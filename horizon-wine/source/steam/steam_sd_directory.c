#define _POSIX_C_SOURCE 200809L
#include "steam_storage.h"
#include "../network_diag.h"
#include <errno.h>
#include <string.h>
#include <switch/services/fs.h>

extern void wine_nx_sd_forget_stat(const char *path);

/* FS takes a full FS_MAX_PATH-byte IPC input buffer, without the mount prefix. */
enum { SD_PATH_NOT_FOUND = 0x202, SD_PATH_EXISTS = 0x402 };
static int fs_errno(Result rc) {
  switch (rc) {
    case SD_PATH_NOT_FOUND: return ENOENT;
    case SD_PATH_EXISTS: return EEXIST;
    case 0x2ee202: return EINVAL;
    case 0x2ee602: return ENAMETOOLONG;
    default: return EIO;
  }
}

/* A name alone cannot establish that an existing object is a directory.
 * Log bytes too: two names can render identically in the text log. */
static void log_name_bytes(const char *name, size_t len, const char *source) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < len;) {
    char bytes[97];
    size_t n = len - i < 48 ? len - i : 48;
    for (size_t j = 0; j < n; ++j) {
      unsigned char c = (unsigned char)name[i+j];
      bytes[2*j] = hex[c >> 4];
      bytes[2*j+1] = hex[c & 15];
    }
    bytes[2*n] = 0;
    network_diag("stage=steam.resume.native op=name_bytes source=%s offset=%zu hex=%s", source, i, bytes);
    i += n;
  }
}

/* Return 1 for the exact directory, 0 for a complete listing without it and
 * -1 for a conflicting type or a service error. */
static int list_child(FsFileSystem *fs, const char *parent, const char *leaf,
                      unsigned mode, const char *pass) {
  FsDir dir;
  Result rc = fsFsOpenDirectory(fs, parent, mode, &dir);
  network_diag("stage=steam.resume.native pass=%s op=open_parent rc=0x%x mode=0x%x path=%s", pass, rc, mode, parent);
  if (R_FAILED(rc)) { errno = fs_errno(rc); return -1; }
  int found = 0, err = 0;
  unsigned int total = 0;
  size_t wanted = strlen(leaf);
  int detail = strcmp(pass, "before_create") != 0;
  for (;;) {
    FsDirectoryEntry entries[4];
    s64 n = 0;
    rc = fsDirRead(&dir, &n, 4, entries);
    if (R_FAILED(rc) || n < 0 || n > 4) {
      err = R_FAILED(rc) ? fs_errno(rc) : EIO;
      break;
    }
    if (!n) break;
    for (s64 i = 0; i < n; ++i) {
      FsDirectoryEntry *e = &entries[i];
      size_t len = strnlen(e->name, sizeof(e->name));
      ++total;
      int match = len == wanted && !memcmp(e->name, leaf, wanted);
      if (match || detail) {
        network_diag("stage=steam.resume.native op=entry type=%d bytes=%zu name=%.*s",
                     e->type, len, (int)len, e->name);
        log_name_bytes(e->name, len, "listing");
      }
      if (!match) continue;
      if (e->type == FsDirEntryType_Dir) found = 1;
      else err = ENOTDIR;
    }
    if (err) break;
  }
  fsDirClose(&dir);
  network_diag("stage=steam.resume.native pass=%s op=read_parent rc=0x%x errno=%d entries=%u found=%d expected=%s",
               pass, rc, err, total, found, leaf);
  errno = err;
  return err ? -1 : found;
}

static int confirm_child(FsFileSystem *fs, char *parent, const char *child, unsigned *created) {
  char *slash = strrchr(parent, '/');
  const char *leaf = child + (slash - parent) + 1;
  *slash = 0;
  if (!*parent) strcpy(parent, "/");
  unsigned mode = FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize;
  FsDirEntryType type = FsDirEntryType_File;
  Result rc = fsFsGetEntryType(fs, parent, &type);
  network_diag("stage=steam.resume.native op=parent_type rc=0x%x type=%d path=%s",
               rc, R_SUCCEEDED(rc) ? (int)type : -1, parent);
  if (R_FAILED(rc)) { errno = fs_errno(rc); return 0; }
  if (type != FsDirEntryType_Dir) { errno = ENOTDIR; return 0; }
  rc = fsFsGetEntryType(fs, child, &type);
  network_diag("stage=steam.resume.native op=get_type rc=0x%x type=%d path=%s",
               rc, R_SUCCEEDED(rc) ? (int)type : -1, child);
  int listed = list_child(fs, parent, leaf, mode, "before_create");
  if (listed < 0) return 0;
  if (R_FAILED(rc) && rc != SD_PATH_NOT_FOUND) { errno = fs_errno(rc); return 0; }
  if (R_SUCCEEDED(rc) && type != FsDirEntryType_Dir) { errno = ENOTDIR; return 0; }
  if (rc == SD_PATH_NOT_FOUND) {
    Result make = fsFsCreateDirectory(fs, child);
    network_diag("stage=steam.resume.native op=create rc=0x%x path=%s", make, child);
    if (make == SD_PATH_NOT_FOUND) {
      Result parent_rc = fsFsGetEntryType(fs, parent, &type);
      network_diag("stage=steam.resume.native op=parent_after_failed_create rc=0x%x type=%d path=%s",
                   parent_rc, R_SUCCEEDED(parent_rc) ? (int)type : -1, parent);
      log_name_bytes(leaf, strlen(leaf), "failed_create");
    }
    if (R_SUCCEEDED(make) && created) ++*created;
    listed = list_child(fs, parent, leaf, mode, "after_create");
    if (R_FAILED(make) && make != SD_PATH_EXISTS) { errno = fs_errno(make); return 0; }
    if (listed < 0) return 0;
    rc = fsFsGetEntryType(fs, child, &type);
    network_diag("stage=steam.resume.native op=recheck_type rc=0x%x type=%d path=%s",
                 rc, R_SUCCEEDED(rc) ? (int)type : -1, child);
    if (R_FAILED(rc)) { errno = fs_errno(rc); return 0; }
    if (type != FsDirEntryType_Dir) { errno = ENOTDIR; return 0; }
  }
  if (!listed) {
    listed = list_child(fs, parent, leaf, FsDirOpenMode_ReadDirs, "dirs_only");
    if (listed < 0) return 0;
  }
  FsDir handle;
  rc = fsFsOpenDirectory(fs, child, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &handle);
  if (R_SUCCEEDED(rc)) fsDirClose(&handle);
  network_diag("stage=steam.resume.native op=open_child rc=0x%x listed=%d accepted_by=%s path=%s",
               rc, listed, R_FAILED(rc) ? "none" : "full_path", child);
  if (R_FAILED(rc)) { errno = fs_errno(rc); return 0; }
  return 1;
}

int steam_storage_confirm_directory(char *path, unsigned *created) {
  if (strncmp(path, "sdmc:/", 6)) { errno = ENODEV; return 0; }
  char child[FS_MAX_PATH] = {0}, parent[FS_MAX_PATH] = {0};
  const char *relative = path + 5;
  size_t len = strlen(relative);
  if (len >= sizeof(child)) { errno = ENAMETOOLONG; return 0; }
  if (len < 2 || relative[len - 1] == '/') { errno = EINVAL; return 0; }
  memcpy(child, relative, len);
  memcpy(parent, child, len);
  FsFileSystem sd;
  Result rc = fsOpenSdCardFileSystem(&sd);
  if (R_FAILED(rc)) {
    network_diag("stage=steam.resume.native op=open_sd rc=0x%x", rc);
    errno = fs_errno(rc);
    return 0;
  }
  int ok = confirm_child(&sd, parent, child, created);
  int err = ok ? 0 : errno;
  fsFsClose(&sd);
  wine_nx_sd_forget_stat(path);
  network_diag("stage=steam.resume.native op=confirm ok=%d errno=%d path=%s", ok, err, child);
  errno = err;
  return ok;
}
