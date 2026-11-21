#define _POSIX_C_SOURCE 200809L
#include "steam_community.h"
#include "steam_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int known_stage(const char *stage)
{
    static const char *const stages[] = {"启动失败", "仅到启动流程", "游戏中遇阻", "可持续游玩", "已通关"};
    for (size_t i = 0; i < sizeof(stages)/sizeof(stages[0]); ++i)
        if (!strcmp(stages[i], stage)) return 1;
    return 0;
}

int community_lookup(const unsigned char *data, size_t size, uint32_t app, struct community_result *out)
{
    static const char header[] = "AppID\t游戏\tCorkDeck 版本\t实测结果\t实测说明\t运行设置\t来源\n";
    *out = (struct community_result){0};
    if (!data || size > COMMUNITY_LIMIT || size <= sizeof(header)-1 ||
        memcmp(data, header, sizeof(header)-1) || data[size-1] != '\n') return 0;
    const unsigned char *p = data + sizeof(header)-1, *end = data + size;
    struct community_result result = {0};
    while (p < end) {
        const unsigned char *newline = memchr(p, '\n', end-p);
        char row[2048], *fields[7];
        size_t len = newline - p;
        if (!len || len >= sizeof(row) || memchr(p, 0, len)) return 0;
        memcpy(row, p, len);
        row[len] = 0;
        fields[0] = row;
        for (unsigned i = 1; i < 7; ++i) {
            char *tab = strchr(fields[i-1], '\t');
            if (!tab || tab == fields[i-1]) return 0;
            *tab = 0;
            fields[i] = tab + 1;
        }
        if (strchr(fields[6], '\t') || strncmp(fields[6], "https://", 8) ||
            strlen(fields[2]) > 80 || strlen(fields[4]) > 240 || !known_stage(fields[3]) ||
            strspn(fields[0], "0123456789") != strlen(fields[0])) return 0;
        unsigned long long id = strtoull(fields[0], NULL, 10);
        if (!id || id > UINT32_MAX) return 0;
        if (id == app) {
            if (!result.count) {
                snprintf(result.stage, sizeof(result.stage), "%s", fields[3]);
                snprintf(result.detail, sizeof(result.detail), "%s · %s", fields[2], fields[4]);
            } else if (strcmp(result.stage, fields[3])) {
                strcpy(result.stage, "实测结果有差异");
                strcpy(result.detail, "不同报告记录了不同进度，详情请查看公共实测列表。");
            }
            ++result.count;
        }
        p = newline + 1;
    }
    *out = result;
    return 1;
}

unsigned char *community_read(const char *root, size_t *size)
{
    char path[512];
    if (snprintf(path, sizeof(path), "%s/compat-reports.tsv", root) >= (int)sizeof(path) ||
        !steam_file_recover(path)) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    long n = -1;
    if (!fseek(f, 0, SEEK_END)) n = ftell(f);
    unsigned char *buf = n > 0 && n <= COMMUNITY_LIMIT && !fseek(f, 0, SEEK_SET) ? malloc((size_t)n) : NULL;
    struct community_result check;
    if (buf && (fread(buf, 1, n, f) != (size_t)n || fgetc(f) != EOF || ferror(f) ||
                !community_lookup(buf, n, 0, &check))) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) *size = n;
    return buf;
}

int community_save(const char *root, const unsigned char *data, size_t size)
{
    char path[512], tmp[520];
    struct community_result check;
    if (!community_lookup(data, size, 0, &check) ||
        snprintf(path, sizeof(path), "%s/compat-reports.tsv", root) >= (int)sizeof(path)) return 0;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    unlink(tmp);
    int fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(tmp); return 0; }
    int ok = fwrite(data, 1, size, f) == size && !fflush(f) && !fsync(fd);
    if (fclose(f)) ok = 0;
    if (ok && steam_file_replace(tmp, path)) return 1;
    unlink(tmp);
    return 0;
}
