/* Translate only installed Steam payloads. The downloader works directly on
 * its physical staging paths; its install marker travels with the final rename. */
#include <switch.h>
#include <sys/iosupport.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "steam/steam_names.h"

static const devoptab_t *raw;
static devoptab_t device;
enum { ROOT_CAP = 256 };
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct steam_names *current;
static char current_root[ROOT_CAP];
static unsigned generation;

static void invalidate(void) {
  pthread_mutex_lock(&lock);
  ++generation;
  current_root[0] = 0;
  steam_names_free(current);
  current = NULL;
  pthread_mutex_unlock(&lock);
}

/* Returns with lock held on success. Reading the marker can itself enter
 * the SD device, so never hold the dictionary mutex while doing file I/O. */
static int acquire(const char *root) {
  char marker[ROOT_CAP + 32];
  snprintf(marker, sizeof(marker), "%s/autorun-steam-install.ini", root);
  for (;;) {
    pthread_mutex_lock(&lock);
    if (!strcasecmp(current_root, root))
      return 1;
    unsigned before = generation;
    pthread_mutex_unlock(&lock);
    struct steam_names *loaded = NULL;
    FILE *f = fopen(marker, "rb");
    if (f) {
      int ok = steam_names_read(&loaded, f);
      fclose(f);
      if (!ok) {
        steam_names_free(loaded);
        return 0;
      }
    } else if (errno != ENOENT)
      return 0;
    pthread_mutex_lock(&lock);
    if (before == generation) {
      steam_names_free(current);
      current = loaded;
      strcpy(current_root, root);
      return 1;
    }
    pthread_mutex_unlock(&lock);
    steam_names_free(loaded);
  }
}

/* fsdev also accepts paths relative to its physical working directory.
 * Resolve dot components before choosing the installation's dictionary. */
static const char *scope(const char *path, char full[FS_MAX_PATH], char root[ROOT_CAP]) {
  static const char prefix[] = "/switch/wine/drive_c/Steam/";
  root[0] = 0;
  char resolved[FS_MAX_PATH];
  if (fsdevTranslatePath(path, NULL, resolved))
    return NULL;
  size_t used = 0;
  const char *p = resolved;
  while (*p) {
    if (*p == '/') {
      ++p;
      continue;
    }
    size_t n = strcspn(p, "/");
    if (n == 2 && !memcmp(p, "..", 2)) {
      while (used && full[--used] != '/') {
      }
    } else if (!(n == 1 && *p == '.')) {
      if (used + 1 + n >= FS_MAX_PATH)
        return NULL;
      full[used++] = '/';
      memcpy(full + used, p, n);
      used += n;
    }
    p += n;
  }
  full[used] = 0;
  if (strncasecmp(full, prefix, sizeof(prefix) - 1))
    return NULL;
  const char *game = full + sizeof(prefix) - 1, *end = game + strcspn(game, "/");
  size_t n = (size_t)(end - full);
  if (end == game || n + 6 > ROOT_CAP)
    return NULL;
  memcpy(root, "sdmc:", 5);
  memcpy(root + 5, full, n);
  root[n + 5] = 0;
  return *end ? end + 1 : end;
}
static const char *translate(struct _reent *r, const char *path, char out[FS_MAX_PATH + 6]) {
  const unsigned char *p = (const unsigned char *)path;
  while (*p && *p < 128)
    ++p;
  if (!*p)
    return path;
  char full[FS_MAX_PATH], root[ROOT_CAP];
  const char *relative = scope(path, full, root);
  if (!relative || !*relative)
    return path;
  /* The marker itself must be accessible while loading its dictionary. */
  if (!strcasecmp(relative, "autorun-steam-install.ini"))
    return path;
  if (!acquire(root)) {
    r->_errno = errno;
    return NULL;
  }
  size_t n = strlen(root);
  memcpy(out, root, n);
  out[n++] = '/';
  int ok = steam_names_path(current, relative, out + n, FS_MAX_PATH + 5 - n);
  pthread_mutex_unlock(&lock);
  if (!ok) {
    r->_errno = ENAMETOOLONG;
    return NULL;
  }
  return out;
}

static int mapped_open(struct _reent *r, void *fd, const char *p, int flags, int mode) {
  char buf[FS_MAX_PATH + 6];
  p = translate(r, p, buf);
  return p ? raw->open_r(r, fd, p, flags, mode) : -1;
}
static int mapped_stat(struct _reent *r, const char *p, struct stat *st) {
  char buf[FS_MAX_PATH + 6];
  p = translate(r, p, buf);
  return p ? raw->stat_r(r, p, st) : -1;
}
static int mapped_mkdir(struct _reent *r, const char *p, int mode) {
  char buf[FS_MAX_PATH + 6];
  p = translate(r, p, buf);
  return p ? raw->mkdir_r(r, p, mode) : -1;
}
static int mapped_rmdir(struct _reent *r, const char *p) {
  char buf[FS_MAX_PATH + 6];
  p = translate(r, p, buf);
  return p ? raw->rmdir_r(r, p) : -1;
}
static int mapped_unlink(struct _reent *r, const char *p) {
  char buf[FS_MAX_PATH + 6];
  p = translate(r, p, buf);
  return p ? raw->unlink_r(r, p) : -1;
}
static int mapped_chdir(struct _reent *r, const char *p) {
  char buf[FS_MAX_PATH + 6];
  p = translate(r, p, buf);
  return p ? raw->chdir_r(r, p) : -1;
}
static int mapped_rename(struct _reent *r, const char *a, const char *b) {
  char x[FS_MAX_PATH + 6], y[FS_MAX_PATH + 6];
  a = translate(r, a, x);
  b = translate(r, b, y);
  if (!a || !b)
    return -1;
  int rc = raw->rename_r(r, a, b);
  if (!rc)
    invalidate();
  return rc;
}
static char *listing_root(DIR_ITER *it) { return (char *)it->dirStruct + raw->dirStateSize; }
static DIR_ITER *mapped_diropen(struct _reent *r, DIR_ITER *it, const char *p) {
  char full[FS_MAX_PATH], root[ROOT_CAP], buf[FS_MAX_PATH + 6];
  scope(p, full, root);
  p = translate(r, p, buf);
  if (!p || !raw->diropen_r(r, it, p))
    return NULL;
  strcpy(listing_root(it), root);
  return it;
}
static int mapped_dirnext(struct _reent *r, DIR_ITER *it, char *name, struct stat *st) {
  int rc = raw->dirnext_r(r, it, name, st);
  if (rc || !*listing_root(it) || !steam_names_alias(name))
    return rc;
  if (!acquire(listing_root(it))) {
    r->_errno = errno;
    return -1;
  }
  const char *original = steam_names_original(current, name);
  if (original)
    strcpy(name, original);
  pthread_mutex_unlock(&lock);
  return 0;
}
const devoptab_t *wine_nx_steam_name_device(const devoptab_t *base) {
  raw = base;
  device = *base;
  device.open_r = mapped_open;
  device.stat_r = mapped_stat;
  device.lstat_r = mapped_stat;
  device.mkdir_r = mapped_mkdir;
  device.rmdir_r = mapped_rmdir;
  device.unlink_r = mapped_unlink;
  device.chdir_r = mapped_chdir;
  device.rename_r = mapped_rename;
  device.dirStateSize += ROOT_CAP;
  device.diropen_r = mapped_diropen;
  device.dirnext_r = mapped_dirnext;
  return &device;
}
