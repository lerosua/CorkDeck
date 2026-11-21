#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_community.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char root[] = "/tmp/corkdeck-community-XXXXXX", path[256];
    size_t size = 0;
    struct community_result result;
    assert(mkdtemp(root));
    assert(!community_read(root, &size));
    assert(community_lookup(steam_community_seed, steam_community_seed_size, 205100, &result));
    assert(result.count == 1 && strstr(result.detail, "24 FPS"));
    assert(community_lookup(steam_community_seed, steam_community_seed_size, 17410, &result));
    assert(result.count == 1 && !strcmp(result.stage, "启动失败"));
    assert(community_lookup(steam_community_seed, steam_community_seed_size, 42, &result) && !result.count);
    const char extra[] = "205100\tGame\t2.0\t可持续游玩\tPlayed 30 minutes\tFEX\thttps://github.com/lerosua/CorkDeck/issues/99\n";
    size_t total = steam_community_seed_size + sizeof(extra)-1;
    unsigned char *updated = malloc(total);
    assert(updated);
    memcpy(updated, steam_community_seed, steam_community_seed_size);
    memcpy(updated + steam_community_seed_size, extra, sizeof(extra)-1);
    assert(community_save(root, updated, total));
    assert(!community_save(root, updated, total-1));
    assert(!community_save(root, (unsigned char *)"<html>404</html>", 16));
    unsigned char *cached = community_read(root, &size);
    assert(cached && size == total && !memcmp(updated, cached, size));
    assert(community_lookup(cached, size, 205100, &result) && result.count == 2);
    assert(!strcmp(result.stage, "实测结果有差异"));
    snprintf(path, sizeof(path), "%s/compat-reports.tsv", root);
    FILE *bad = fopen(path, "wb");
    assert(bad && fputs("truncated", bad) >= 0 && !fclose(bad));
    assert(!community_read(root, &size));
    free(cached);
    free(updated);
    assert(!unlink(path) && !rmdir(root));
    puts("Bundled reports, offline cache, failed update, unknown game and conflicting reports passed.");
}
