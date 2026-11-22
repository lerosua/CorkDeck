#include "../source/steam/steam_storage.h"
#include <switch/services/fs.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char parent[] = "/switch/wine/steam/staging/2004770/art-picture";
static const char child[] = "/switch/wine/steam/staging/2004770/art-picture/多语言";
static char path[] = "sdmc:/switch/wine/steam/staging/2004770/art-picture/多语言";
static unsigned sessions, handles, creates;
static int exists, invisible, wrong_type, read_failure, visible_dirs_only, open_failure;
static Result create_error;
void wine_nx_sd_forget_stat(const char *p) { assert(!strcmp(p, path)); }
Result fsOpenSdCardFileSystem(FsFileSystem *s) { ++sessions; s->open = 1; return 0; }
void fsFsClose(FsFileSystem *s) { assert(s->open && sessions && !handles); --sessions; }
Result fsFsGetEntryType(FsFileSystem *s, const char *p, FsDirEntryType *type) {
  assert(s->open);
  if (!strcmp(p, parent)) { *type = FsDirEntryType_Dir; return 0; }
  assert(!strcmp(p, child));
  if (!exists) return 0x202;
  *type = wrong_type ? FsDirEntryType_File : FsDirEntryType_Dir;
  return 0;
}
Result fsFsCreateDirectory(FsFileSystem *s, const char *p) {
  assert(s->open && !strcmp(p, child));
  ++creates;
  if (create_error) return create_error;
  if (exists) return 0x402;
  exists = 1;
  return 0;
}
Result fsFsOpenDirectory(FsFileSystem *s, const char *p, unsigned mode, FsDir *d) {
  assert(s->open && (mode & FsDirOpenMode_ReadDirs));
  assert(!strcmp(p, parent) || (exists && !wrong_type && !strcmp(p, child)));
  if (open_failure && !strcmp(p, child)) return 0x202;
  ++handles;
  d->read = 0;
  d->dirs_only = mode == FsDirOpenMode_ReadDirs;
  return 0;
}
Result fsDirRead(FsDir *d, s64 *n, size_t max, FsDirectoryEntry *out) {
  assert(handles && max >= 1);
  if (read_failure) return 0xf601;
  *n = 0;
  if (!d->read++) {
    strcpy(out->name, "多語言"); /* A similar name must never satisfy a byte-for-byte match. */
    out->type = FsDirEntryType_Dir;
    *n = 1;
  } else if (d->read == 2 && exists && (!invisible || (visible_dirs_only && d->dirs_only))) {
    strcpy(out->name, "多语言");
    out->type = wrong_type ? FsDirEntryType_File : FsDirEntryType_Dir;
    *n = 1;
  }
  return 0;
}
void fsDirClose(FsDir *d) { (void)d; assert(handles); --handles; }
int main(void) {
  unsigned made = 0;
  assert(steam_storage_confirm_directory(path, &made) && creates == 1 && made == 1);
  assert(steam_storage_confirm_directory(path, &made) && creates == 1 && made == 1);
  invisible = 1;
  assert(steam_storage_confirm_directory(path, &made) && creates == 1);
  open_failure = 1;
  assert(!steam_storage_confirm_directory(path, &made) && errno == ENOENT && creates == 1);
  open_failure = 0;
  exists = 0;
  assert(steam_storage_confirm_directory(path, &made) && creates == 2 && made == 2);
  visible_dirs_only = 1;
  assert(steam_storage_confirm_directory(path, &made) && creates == 2);
  invisible = 0; wrong_type = 1;
  assert(!steam_storage_confirm_directory(path, &made) && errno == ENOTDIR && creates == 2);
  wrong_type = 0; read_failure = 1;
  assert(!steam_storage_confirm_directory(path, &made) && errno == EIO && creates == 2);
  read_failure = 0;
  exists = 0;
  create_error = 0x202;
  assert(!steam_storage_confirm_directory(path, &made) && errno == ENOENT && creates == 3 && !exists);
  assert(!sessions && !handles && made == 2);
  puts("Native SD directory: original UTF-8 path, unlisted directory reuse and creation, open failure, type conflict and read failure passed");
}
