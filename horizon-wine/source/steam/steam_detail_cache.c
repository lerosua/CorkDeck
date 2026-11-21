#define _POSIX_C_SOURCE 200809L
#include "steam_detail_cache.h"
#include "steam_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

/* v1 is a fixed-size little-endian snapshot. Bind the checksum to the
 * filename too, so a misplaced SD file cannot be shown for another game. */
static int cache_path(char path[512], const char *root, uint32_t app, uint64_t account,
                      const char *language, int create)
{
    if (!root || !*root || strlen(root) >= 256 || !app) return 0;
    if (language && (!account || !*language || strlen(language) >= 32 ||
        strspn(language, "abcdefghijklmnopqrstuvwxyz") != strlen(language))) return 0;
    if (create) {
        snprintf(path, 512, "%s/steam", root);
        if (mkdir(path, 0700) && errno != EEXIST) return 0;
        strcat(path, "/details");
        if (mkdir(path, 0700) && errno != EEXIST) return 0;
    }
    if (language)
        snprintf(path, 512, "%s/steam/details/%u-%llu-%s.v1", root, app, (unsigned long long)account, language);
    else
        snprintf(path, 512, "%s/steam/details/%u-windows.v1", root, app);
    return 1;
}

static uint32_t checksum(const char *path, const void *data, size_t size)
{
    const char *name = strrchr(path, '/') + 1;
    return crc32(crc32(0, (const Bytef *)name, strlen(name)), data, size);
}

int steam_detail_read(const char *root, uint32_t app, uint64_t account, const char *language,
                      void *data, size_t size)
{
    char path[512];
    if (!cache_path(path, root, app, account, language, 0) || !steam_file_recover(path)) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t crc;
    int ok = fread(&crc, sizeof(crc), 1, f) == 1 && fread(data, size, 1, f) == 1 &&
             fgetc(f) == EOF && !ferror(f) && crc == checksum(path, data, size);
    fclose(f);
    return ok;
}

int steam_detail_write(const char *root, uint32_t app, uint64_t account, const char *language,
                       const void *data, size_t size)
{
    char path[512], tmp[520];
    if (!cache_path(path, root, app, account, language, 1)) return 0;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(tmp); return 0; }
    uint32_t crc = checksum(path, data, size);
    int ok = fwrite(&crc, sizeof(crc), 1, f) == 1 && fwrite(data, size, 1, f) == 1 &&
             !fflush(f) && !fsync(fd);
    if (fclose(f)) ok = 0;
    if (ok && steam_file_replace(tmp, path)) return 1;
    unlink(tmp);
    return 0;
}
