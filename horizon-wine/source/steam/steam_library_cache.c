#define _POSIX_C_SOURCE 200809L
#include "steam_library_cache.h"
#include "steam_file.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

/* Versioned little-endian Switch format. Install state comes from the card. */
struct cache_header {
    char magic[8];
    uint64_t account;
    struct steam_library_stamp stamp;
    uint32_t count, checksum;
};
struct cache_row {
    uint64_t lender;
    uint32_t app;
    char name[128], icon[41];
    unsigned char reserved[3];
};
_Static_assert(sizeof(struct cache_header) == 64, "cache header layout");
_Static_assert(sizeof(struct cache_row) == 184, "cache row layout");

static int cache_path(const char *root, const char *suffix, char out[512])
{
    int n = snprintf(out, 512, "%s/steam-library.cache%s", root, suffix);
    return *root && n > 0 && n < 512;
}

int steam_library_fresh(const struct steam_library_stamp *stamp, const char *language, time_t now)
{
    return now > 0 && stamp->synced && stamp->synced <= (uint64_t)now &&
           (uint64_t)now - stamp->synced < 24 * 60 * 60 && !strcmp(stamp->language, language);
}

int steam_library_cache_load(const char *root, uint64_t account, struct steam_library *out,
                             struct steam_library_stamp *stamp)
{
    char path[512];
    struct cache_header h;
    struct steam_library list = {.account = account};
    FILE *fp;
    if (!account || !cache_path(root, "", path) || !steam_file_recover(path) || !(fp = fopen(path, "rb")))
        return 0;
    if (fread(&h, sizeof(h), 1, fp) != 1 || memcmp(h.magic, "CDSLIB01", 8) ||
        h.account != account || h.count > STEAM_LIBRARY_LIMIT || !memchr(h.stamp.language, 0, sizeof(h.stamp.language)))
        goto fail;
    uint32_t sum = crc32(0, (const Bytef *)&h, offsetof(struct cache_header, checksum));
    if (h.count && !(list.games = calloc(h.count, sizeof(*list.games)))) goto fail;
    for (; list.count < h.count; list.count++) {
        struct cache_row row;
        struct steam_game *g = &list.games[list.count];
        if (fread(&row, sizeof(row), 1, fp) != 1 || !row.app || row.app > INT32_MAX ||
            !row.name[0] || !memchr(row.name, 0, sizeof(row.name)) || row.icon[40] ||
            (row.icon[0] && strspn(row.icon, "0123456789abcdef") != 40)) goto fail;
        sum = crc32(sum, (const Bytef *)&row, sizeof(row));
        g->app_id = row.app;
        g->shared = row.lender != 0;
        g->lender = row.lender;
        strcpy(g->name, row.name);
        strcpy(g->icon_hash, row.icon);
    }
    if (sum != h.checksum || fgetc(fp) != EOF || ferror(fp)) goto fail;
    fclose(fp);
    steam_library_clear(out);
    *out = list;
    *stamp = h.stamp;
    return 1;
fail:
    fclose(fp);
    steam_library_clear(&list);
    return 0;
}

int steam_library_cache_save(const char *root, const struct steam_library *list,
                             const struct steam_library_stamp *stamp)
{
    struct cache_header h = {.account = list->account, .stamp = *stamp};
    char path[512], tmp[512];
    int fd, ok;
    FILE *fp;
    if (!list->account || list->count > STEAM_LIBRARY_LIMIT ||
        !cache_path(root, "", path) || !cache_path(root, ".tmp", tmp)) return 0;
    unlink(tmp);
    fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) return 0;
    if (!(fp = fdopen(fd, "wb"))) { close(fd); unlink(tmp); return 0; }
    memcpy(h.magic, "CDSLIB01", sizeof(h.magic));
    h.count = (uint32_t)list->count;
    h.checksum = crc32(0, (const Bytef *)&h, offsetof(struct cache_header, checksum));
    ok = fwrite(&h, sizeof(h), 1, fp) == 1;
    for (size_t i = 0; ok && i < list->count; i++) {
        const struct steam_game *g = &list->games[i];
        struct cache_row row = {.lender = g->lender, .app = g->app_id};
        memcpy(row.name, g->name, sizeof(row.name));
        memcpy(row.icon, g->icon_hash, sizeof(row.icon));
        h.checksum = crc32(h.checksum, (const Bytef *)&row, sizeof(row));
        ok = fwrite(&row, sizeof(row), 1, fp) == 1;
    }
    ok = ok && fseek(fp, 0, SEEK_SET) == 0 && fwrite(&h, sizeof(h), 1, fp) == 1 &&
         fflush(fp) == 0 && fsync(fd) == 0;
    if (fclose(fp)) ok = 0;
    if (ok && steam_file_replace(tmp, path)) return 1;
    unlink(tmp);
    return 0;
}

void steam_library_cache_forget(const char *root)
{
    static const char *const suffix[] = {".previous", ".tmp", ""};
    char path[512];
    for (size_t i = 0; i < sizeof(suffix) / sizeof(suffix[0]); i++)
        if (cache_path(root, suffix[i], path)) unlink(path);
}
