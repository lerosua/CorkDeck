#ifndef STEAM_TEST_NATIVE_FS_H
#define STEAM_TEST_NATIVE_FS_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t Result;
typedef int64_t s64;
typedef struct { int open; } FsFileSystem;
typedef struct { int read, dirs_only; } FsDir;
typedef enum { FsDirEntryType_Dir, FsDirEntryType_File } FsDirEntryType;
enum { FS_MAX_PATH = 0x301, FsDirOpenMode_ReadDirs = 1, FsDirOpenMode_ReadFiles = 2 };
#define FsDirOpenMode_NoFileSize (1u << 31)
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
typedef struct { char name[FS_MAX_PATH]; int type; } FsDirectoryEntry;
Result fsOpenSdCardFileSystem(FsFileSystem *);
void fsFsClose(FsFileSystem *);
Result fsFsOpenDirectory(FsFileSystem *,const char *,unsigned,FsDir *);
Result fsDirRead(FsDir *,s64 *,size_t,FsDirectoryEntry *);
void fsDirClose(FsDir *);
Result fsFsGetEntryType(FsFileSystem *,const char *,FsDirEntryType *);
Result fsFsCreateDirectory(FsFileSystem *,const char *);
#endif
