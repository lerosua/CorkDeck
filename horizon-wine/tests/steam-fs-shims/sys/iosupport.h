#ifndef STEAM_TEST_IOSUPPORT_H
#define STEAM_TEST_IOSUPPORT_H
#include <sys/stat.h>
struct _reent {
  int _errno;
};
typedef struct {
  void *dirStruct;
} DIR_ITER;
typedef struct {
  int dirStateSize;
  int (*open_r)(struct _reent *, void *, const char *, int, int);
  int (*stat_r)(struct _reent *, const char *, struct stat *);
  int (*lstat_r)(struct _reent *, const char *, struct stat *);
  int (*mkdir_r)(struct _reent *, const char *, int);
  int (*rmdir_r)(struct _reent *, const char *);
  int (*unlink_r)(struct _reent *, const char *);
  int (*chdir_r)(struct _reent *, const char *);
  int (*rename_r)(struct _reent *, const char *, const char *);
  DIR_ITER *(*diropen_r)(struct _reent *, DIR_ITER *, const char *);
  int (*dirnext_r)(struct _reent *, DIR_ITER *, char *, struct stat *);
} devoptab_t;
#endif
