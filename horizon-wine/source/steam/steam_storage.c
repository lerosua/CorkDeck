#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "steam_storage.h"
#include "steam_content.h"
#include "../network_diag.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Opening a directory goes to the SD filesystem even when lstat has a cached
 * positive or negative entry. libnx's SD device does not support symlinks. */
static int open_directory(const char *path) {
#ifndef __SWITCH__
  struct stat st;
  if (lstat(path, &st)) return 0;
  if (!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    return 0;
  }
#endif
  DIR *d = opendir(path);
  return d && !closedir(d);
}

#ifndef __SWITCH__
/* Distinguish an absent entry from a failed listing. A usable handle for the
 * child alone can disagree with the SD card's listing of its parent. */
static int listed_directory(char *path, const char *pass) {
  char *slash = strrchr(path, '/');
  const char *name = slash + 1;
  *slash = 0;
  DIR *d = opendir(path);
  *slash = '/';
  int err = d ? 0 : errno;
  unsigned count = 0;
  int found = 0;
  if (d) {
    for (;;) {
      errno = 0;
      struct dirent *entry = readdir(d);
      if (!entry) {
        err = errno;
        break;
      }
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
      ++count;
      network_diag("stage=steam.resume.list pass=%s type=%u name=%s", pass,
                   (unsigned)entry->d_type, entry->d_name);
      if (strcmp(entry->d_name, name)) continue;
      found = 1;
      if (entry->d_type == DT_UNKNOWN) {
        if (!open_directory(path)) err = errno;
      } else if (entry->d_type != DT_DIR) {
        err = ENOTDIR;
      }
      if (err) break;
    }
    if (closedir(d) && !err) err = errno;
  }
  network_diag("stage=steam.resume.list pass=%s path=%s entries=%u found=%d errno=%d",
               pass, path, count, found, err);
  errno = err;
  return err ? -1 : found;
}

int steam_storage_confirm_directory(char *path, unsigned *created) {
  int listed = listed_directory(path, "before_create");
  if (listed < 0) return 0;
  if (open_directory(path)) return 1;
  if (errno != ENOENT) return 0;
  int rc = mkdir(path, 0777);
  int err = rc ? errno : 0;
  network_diag("stage=steam.resume.component op=explicit_mkdir path=%s ret=%d errno=%d",
               path, rc, err);
  if (!rc && created) ++*created;
  listed = listed_directory(path, "after_create");
  if (rc && err != EEXIST) {
    errno = err;
    return 0;
  }
  return listed >= 0 && open_directory(path);
}

#endif

int steam_storage_needs_parent_listing(const char *path) {
  const char *art = strstr(path, "/art-picture/");
  if (!art) return 0;
  /* The same depot also has Vietnamese and English art under Chinese names.
   * Check every non-ASCII directory in this tree, including mixed names. */
  const unsigned char *leaf = (const unsigned char *)strrchr(art, '/') + 1;
  for (; *leaf; ++leaf)
    if (*leaf >= 0x80) return 1;
  return 0;
}

static int repair_component(const char *path, unsigned *created, const char **op) {
  *op = "open_directory";
  if (open_directory(path)) return 1;
  if (errno != ENOENT) return 0;
  *op = "mkdir";
  int rc = mkdir(path, 0777);
  if (!rc) ++*created;
  else if (errno != EEXIST) return 0;
  *op = "verify_directory";
  return open_directory(path);
}

int steam_storage_repair_dirs(const char *stage, const char *relative, int directory_entry) {
  char path[1024], entry[384];
  size_t root_len = strlen(stage);
  if (strlen(relative) >= sizeof(entry) || root_len + 1 + strlen(relative) >= sizeof(path)) {
    errno = ENAMETOOLONG;
    return 0;
  }
  strcpy(entry, relative);
  if (!steam_content_path(entry)) {
    errno = EINVAL;
    return 0;
  }
  strcpy(path, stage);
  unsigned int created = 0;
  const char *op = "open_stage";
  if (!open_directory(path)) goto failed;
  path[root_len] = '/';
  strcpy(path + root_len + 1, entry);
  for (char *p = path + root_len + 1; ; ++p) {
    if (*p && *p != '/') continue;
    char separator = *p;
    if (!separator && !directory_entry) break;
    *p = 0;
    if (steam_storage_needs_parent_listing(path)) {
      op = "confirm_parent_listing";
      if (!steam_storage_confirm_directory(path, &created)) goto failed;
    } else if (!repair_component(path, &created, &op)) {
      goto failed;
    }
    if (!separator) break;
    *p = separator;
  }
  network_diag("stage=steam.resume.repair result=ready created=%u entry=%s", created, relative);
  return 1;
failed: {
    int err = errno;
    network_diag("stage=steam.resume.repair result=failed op=%s errno=%d path=%s", op, err, path);
    errno = err;
    return 0;
  }
}
static int dir(const char *p, int create) {
  struct stat s;
  if (!lstat(p, &s))
    return S_ISDIR(s.st_mode) && !S_ISLNK(s.st_mode);
  return create && errno == ENOENT && !mkdir(p, 0777);
}
static int base(const char *root, char out[512], int create) {
  char p[512];
  int n = snprintf(p, sizeof(p), "%s/drive_c", root);
  if (n < 0 || (size_t)n >= sizeof(p) || !dir(p, create))
    return 0;
  n = snprintf(out, 512, "%s/Steam", p);
  return n > 0 && n < 512 && dir(out, create);
}
static int steam_dir(const char *root, const char *sub, char out[512]) {
  char p[512];
  int n = snprintf(p, sizeof(p), "%s/steam", root);
  if (n < 0 || (size_t)n >= sizeof(p) || !dir(p, 1))
    return 0;
  n = snprintf(out, 512, "%s/%s", p, sub);
  return n > 0 && n < 512 && dir(out, 1);
}
static int leaf(const char *p) {
  char copy[256];
  size_t n = strlen(p);
  if (!n || n >= sizeof(copy) || strchr(p, '/') || strchr(p, '\\'))
    return 0;
  memcpy(copy, p, n + 1);
  return steam_content_path(copy);
}
static int component(const char *p) { return strlen(p) <= 180 && leaf(p); }
static int marker(const char *directory, uint32_t app) {
  char p[1024];
  struct stat s;
  FILE *f;
  unsigned int id = 0;
  int version = 0, ok;
  if (!dir(directory, 0) ||
      snprintf(p, sizeof(p), "%s/autorun-steam-install.ini", directory) >=
          (int)sizeof(p) ||
      lstat(p, &s) || !S_ISREG(s.st_mode))
    return 0;
  f = fopen(p, "r");
  if (!f)
    return 0;
  ok = fscanf(f, "autorun_steam_install=%d\nappid=%u", &version, &id) == 2 &&
       version == 1 && id == app;
  fclose(f);
  return ok;
}
static int location(const char *root, uint32_t app, char out[512],
                    int installed) {
  char b[512], p[768], name[192] = {0};
  struct stat s;
  FILE *f;
  int n;
  out[0] = 0;
  if (!app || !base(root, b, 0))
    return 0;
  n = snprintf(p, sizeof(p), "%s/steam/installed/%u.path", root, app);
  if (n < 0 || (size_t)n >= sizeof(p))
    return 0;
  if (!lstat(p, &s) && S_ISREG(s.st_mode) && (f = fopen(p, "r"))) {
    int ok = fgets(name, sizeof(name), f) != NULL;
    fclose(f);
    name[strcspn(name, "\r\n")] = 0;
    if (ok && component(name)) {
      n = snprintf(out, 512, "%s/%s", b, name);
      if (n > 0 && n < 512 && (!installed || marker(out, app)))
        return 1;
    }
  }
  n = snprintf(out, 512, "%s/%u", b, app);
  if (n > 0 && n < 512 && (!installed || marker(out, app)))
    return 1;
  out[0] = 0;
  return 0;
}
int steam_storage_directory(const char *root, uint32_t app, char out[512]) {
  return location(root, app, out, 1);
}
int steam_storage_pending(const char *root, uint32_t app, char out[512]) {
  char p[768], d[512];
  if (snprintf(d, sizeof(d), "%s/steam", root) >= (int)sizeof(d) ||
      !dir(d, 0) || snprintf(p, sizeof(p), "%s/trash", d) >= (int)sizeof(p) ||
      !dir(p, 0))
    return 0;
  if (snprintf(p, sizeof(p), "%s/trash/%u", d, app) >= (int)sizeof(p) ||
      !marker(p, app))
    return 0;
  return location(root, app, out, 0);
}
int steam_storage_plan(const char *root, uint32_t app, const char *title,
                       char out[1024]) {
  char b[512], name[160];
  size_t i, n = 0;
  struct stat s;
  if (!app || !base(root, b, 1))
    return 0;
  for (i = 0; title && title[i] && n < 120; ++i) {
    unsigned char c = (unsigned char)title[i];
    name[n++] = c < 32 || strchr("<>:\"/\\|?*", c) ? '_' : (char)c;
  }
  /* Drop a partial UTF-8 character at the byte limit. */
  if (title && title[i])
    while (n && ((unsigned char)title[i] & 0xc0) == 0x80) {
      --n;
      --i;
    }
  while (n && (name[n - 1] == ' ' || name[n - 1] == '.'))
    --n;
  name[n] = 0;
  int unicode_title = 0;
  for (size_t j = 0; j < n; ++j) if ((unsigned char)name[j] >= 0x80) unicode_title = 1;
  if (!component(name) || unicode_title)
    snprintf(name, sizeof(name), "Steam game %u", app);
  /* Prefix DOS device names, including forms such as CON.ext. */
  {
    char upper[8] = {0};
    size_t k;
    for (k = 0; k < 7 && name[k] && name[k] != '.'; ++k)
      upper[k] = name[k] >= 'a' && name[k] <= 'z' ? name[k] - 32 : name[k];
    if (!strcmp(upper, "CON") || !strcmp(upper, "PRN") ||
        !strcmp(upper, "AUX") || !strcmp(upper, "NUL") ||
        (!strncmp(upper, "COM", 3) && upper[3] >= '1' && upper[3] <= '9' &&
         !upper[4]) ||
        (!strncmp(upper, "LPT", 3) && upper[3] >= '1' && upper[3] <= '9' &&
         !upper[4])) {
      memmove(name + 1, name, strlen(name) + 1);
      name[0] = '_';
    }
  }
  if (snprintf(out, 1024, "%s/%s", b, name) >= 1024)
    return 0;
  if (lstat(out, &s) && errno == ENOENT)
    return 1;
  if (snprintf(out, 1024, "%s/%s (%u)", b, name, app) >= 1024)
    return 0;
  return lstat(out, &s) && errno == ENOENT;
}
int steam_storage_record(const char *root, uint32_t app,
                         const char *directory) {
  char b[512], d[512], p[768], tmp[780];
  const char *name;
  struct stat s;
  FILE *f;
  int ok;
  if (!base(root, b, 0) || strncmp(directory, b, strlen(b)) ||
      directory[strlen(b)] != '/')
    return 0;
  name = directory + strlen(b) + 1;
  if (!component(name) || !steam_dir(root, "installed", d))
    return 0;
  snprintf(p, sizeof(p), "%s/%u.path", d, app);
  snprintf(tmp, sizeof(tmp), "%s.tmp", p);
  if ((!lstat(p, &s) && !S_ISREG(s.st_mode)) ||
      (!lstat(tmp, &s) && !S_ISREG(s.st_mode)))
    return 0;
  f = fopen(tmp, "wb");
  if (!f)
    return 0;
  ok = fprintf(f, "%s\n", name) > 0 && !fflush(f) && !fsync(fileno(f));
  if (fclose(f))
    ok = 0;
  if (!ok)
    return 0;
  /* The card does not rename over a file that is there, and a record of an
   * installation that is gone may be: an install that reached its last step
   * failed there with error 3101. */
  if (!rename(tmp, p))
    return 1;
  if (unlink(p) && errno != ENOENT)
    return 0;
  return !rename(tmp, p);
}
static int tree(const char *p, unsigned int depth, int erase,
                int (*tick)(void *), void *context) {
  DIR *d;
  struct dirent *e;
  struct stat s;
  char child[1024];
  int ok = 1;
  if ((tick && !tick(context)) || depth > 24 || lstat(p, &s) ||
      S_ISLNK(s.st_mode))
    return 0;
  if (S_ISREG(s.st_mode))
    return !erase || !unlink(p);
  if (!S_ISDIR(s.st_mode) || (d = opendir(p)) == NULL)
    return 0;
  for (;;) {
    errno = 0;
    e = readdir(d);
    if (!e) {
      if (errno)
        ok = 0;
      break;
    }
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    if (erase && !depth && !strcmp(e->d_name, "autorun-steam-install.ini"))
      continue;
    if (!leaf(e->d_name) ||
        snprintf(child, sizeof(child), "%s/%s", p, e->d_name) >=
            (int)sizeof(child) ||
        !tree(child, depth + 1, erase, tick, context)) {
      ok = 0;
      break;
    }
  }
  closedir(d);
  if (ok && erase && !depth) {
    if (snprintf(child, sizeof(child), "%s/autorun-steam-install.ini", p) >=
            (int)sizeof(child) ||
        unlink(child))
      ok = 0;
  }
  return ok && (!erase || !rmdir(p));
}
unsigned int steam_storage_uninstall(const char *root, uint32_t app,
                                     int (*tick)(void *), void *context) {
  char source[512], d[512], trash[768], p[768];
  struct stat s;
  if (!app || !steam_dir(root, "trash", d))
    return 3101;
  snprintf(trash, sizeof(trash), "%s/%u", d, app);
  if (steam_storage_directory(root, app, source)) {
    if (!lstat(trash, &s) || errno != ENOENT ||
        !tree(source, 0, 0, tick, context))
      return 3104;
    if (rename(source, trash))
      return 3101;
  } else if (!marker(trash, app))
    return 3104;
  if (!tree(trash, 0, 0, tick, context) || !tree(trash, 0, 1, tick, context))
    return errno == ENOSPC ? 3102 : 3101;
  snprintf(p, sizeof(p), "%s/steam/installed/%u.path", root, app);
  if (unlink(p) && errno != ENOENT)
    return 3101;
  snprintf(p, sizeof(p), "%s/steam/selected/%u.exe", root, app);
  if (unlink(p) && errno != ENOENT)
    return 3101;
  return 0;
}
static int discard_tree(const char *p, unsigned int depth) {
  struct stat s;
  struct dirent *e;
  DIR *d;
  char child[1024];
  int error = 0;
  if (depth > 24) return ELOOP;
  if (lstat(p, &s)) return errno == ENOENT ? 0 : (errno ? errno : EIO);
  /* Unlink owned leaf links without opening their targets. Ancestor links are
   * refused below, before any deletion, and all producers are already joined. */
  if (S_ISREG(s.st_mode) || S_ISLNK(s.st_mode))
    return unlink(p) ? (errno ? errno : EIO) : 0;
  if (!S_ISDIR(s.st_mode)) return EINVAL;
  d = opendir(p);
  if (!d) return errno ? errno : EIO;
  for (;;) {
    errno = 0;
    e = readdir(d);
    if (!e) {error = errno; break;}
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (!leaf(e->d_name)) {error = EINVAL; break;}
    if (snprintf(child, sizeof(child), "%s/%s", p, e->d_name) >= (int)sizeof(child)) {
      error = ENAMETOOLONG; break;
    }
    error = discard_tree(child, depth + 1);
    if (error) break;
  }
  if (closedir(d) && !error) error = errno ? errno : EIO;
  if (!error && rmdir(p)) error = errno ? errno : EIO;
  return error;
}
unsigned int steam_storage_discard(const char *root, uint32_t app,
                                   unsigned int *filesystem_error) {
  char directories[3][768], p[1024];
  static const char *suffixes[] = {"", ".plan", ".plan.tmp", ".plan.previous"};
  struct stat s;
  unsigned int i;
  int error = app ? 0 : EINVAL;
  *filesystem_error = 0;
  if (!error) {
    if (lstat(root, &s)) error = errno ? errno : EIO;
    else if (!S_ISDIR(s.st_mode) || S_ISLNK(s.st_mode)) error = EINVAL;
  }
  if (snprintf(directories[0], sizeof(directories[0]), "%s/steam", root) >= (int)sizeof(directories[0]) ||
      snprintf(directories[1], sizeof(directories[1]), "%s/steam/staging", root) >= (int)sizeof(directories[1]) ||
      snprintf(directories[2], sizeof(directories[2]), "%s/steam/manifests", root) >= (int)sizeof(directories[2])) error = ENAMETOOLONG;
  /* Check every ancestor before touching any AppID leaf. Never create folders
   * while cancelling a game that has not written anything yet. */
  for (i = 0; !error && i < 3; ++i) {
    if (lstat(directories[i], &s)) {
      if (errno != ENOENT) error = errno ? errno : EIO;
    } else if (!S_ISDIR(s.st_mode) || S_ISLNK(s.st_mode)) error = EINVAL;
  }
  for (i = 0; !error && i < sizeof(suffixes)/sizeof(*suffixes); ++i) {
    if (snprintf(p, sizeof(p), "%s/%u%s", directories[1], app, suffixes[i]) >= (int)sizeof(p)) error = ENAMETOOLONG;
    else error = discard_tree(p, 0);
  }
  if (!error) {
    if (snprintf(p, sizeof(p), "%s/%u", directories[2], app) >= (int)sizeof(p)) error = ENAMETOOLONG;
    else error = discard_tree(p, 0);
  }
  *filesystem_error = (unsigned int)error;
  return error ? 3112 : 0;
}
