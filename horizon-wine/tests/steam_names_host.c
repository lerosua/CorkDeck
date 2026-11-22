#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>
#include <sys/iosupport.h>
#include "../source/steam/steam_names.h"

extern const devoptab_t *wine_nx_steam_name_device(const devoptab_t *);
static char cwd[FS_MAX_PATH] = "/";
int fsdevTranslatePath(const char *p, FsFileSystem **unused, char *out) {
  (void)unused;
  if (!strncmp(p, "sdmc:", 5))
    p += 5;
  int n =
      *p == '/' ? snprintf(out, FS_MAX_PATH, "%s", p) : snprintf(out, FS_MAX_PATH, "%s/%s", cwd, p);
  return n < 0 || n >= FS_MAX_PATH ? -1 : 0;
}
static int fs_open(struct _reent *r, void *fd, const char *p, int flags, int mode) {
  (void)r;
  (void)fd;
  return open(p, flags, mode);
}
static DIR_ITER *fs_diropen(struct _reent *r, DIR_ITER *it, const char *p) {
  (void)r;
  DIR **d = it->dirStruct;
  *d = opendir(p);
  return *d ? it : NULL;
}
static int fs_next(struct _reent *r, DIR_ITER *it, char *name, struct stat *st) {
  (void)r;
  (void)st;
  struct dirent *de = readdir(*(DIR **)it->dirStruct);
  if (!de)
    return -1;
  strcpy(name, de->d_name);
  return 0;
}
static void dirs(char *path) {
  for (char *p = path; *p; ++p)
    if (*p == '/') {
      *p = 0;
      assert(!mkdir(path, 0700) || errno == EEXIST);
      *p = '/';
    }
}
int main(void) {
  char temp[] = "/tmp/corkdeck-names-XXXXXX";
  assert(mkdtemp(temp) && !chdir(temp));
  const char *root = "sdmc:/switch/wine/drive_c/Steam/Fixture";
  const char *member = "素材/繁体/画室😀.png";
  struct steam_names *map = NULL;
  char disk[512], absolute[1024], physical[1024], marker[512];
  /* Reserve the exact spelling the allocator would otherwise use. */
  assert(steam_names_collect(&map, member) && steam_names_finish(map));
  assert(steam_names_path(map, member, disk, sizeof(disk)));
  char literal[12];
  strcpy(literal, strrchr(disk, '/') + 1);
  steam_names_free(map);
  map = NULL;
  assert(steam_names_collect(&map, literal) && steam_names_collect(&map, member) &&
         steam_names_finish(map));
  assert(steam_names_path(map, member, disk, sizeof(disk)) &&
         strcmp(strrchr(disk, '/') + 1, literal));
  snprintf(physical, sizeof(physical), "%s/%s", root, disk);
  dirs(physical);
  FILE *f = fopen(physical, "wb");
  assert(f && fputs("pixels", f) >= 0 && !fclose(f));
  snprintf(marker, sizeof(marker), "%s/autorun-steam-install.ini", root);
  f = fopen(marker, "wb");
  assert(f && steam_names_write(map, f) && !fclose(f));
  steam_names_free(map);
  devoptab_t raw = {.open_r = fs_open,
                    .diropen_r = fs_diropen,
                    .dirnext_r = fs_next,
                    .dirStateSize = sizeof(DIR *)};
  const devoptab_t *device = wine_nx_steam_name_device(&raw);
  struct _reent r = {0};
  struct stat st;
  snprintf(absolute, sizeof(absolute), "%s/素材/繁体", root);
  DIR_ITER it = {calloc(1, (size_t)device->dirStateSize)};
  assert(it.dirStruct && device->diropen_r(&r, &it, absolute));
  char entry[256], file[1400], bytes[6];
  int found = 0;
  while (!device->dirnext_r(&r, &it, entry, &st)) {
    if (entry[0] == '.')
      continue;
    assert(!strcmp(entry, "画室😀.png"));
    found++;
    snprintf(file, sizeof(file), "%s/%s", absolute, entry);
    int fd = device->open_r(&r, NULL, file, O_RDONLY, 0);
    assert(fd >= 0 && read(fd, bytes, 6) == 6 && !memcmp(bytes, "pixels", 6) && !close(fd));
  }
  assert(found == 1 && !closedir(*(DIR **)it.dirStruct));
  free(it.dirStruct);
  strcpy(cwd, physical + 5);
  *strrchr(cwd, '/') = 0;
  int fd = device->open_r(&r, NULL, "画室😀.png", O_RDONLY, 0);
  assert(fd >= 0 && !close(fd));
  assert(!unlink(physical) && !unlink(marker));
  char *p;
  while ((p = strrchr(physical, '/'))) {
    *p = 0;
    assert(!rmdir(physical));
  }
  assert(!chdir("/") && !rmdir(temp));
  puts("Steam SD name map: reserved ASCII collision, persisted names, directory enumeration and "
       "opening returned Unicode names passed");
}
