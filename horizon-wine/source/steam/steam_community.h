#ifndef CORKDECK_STEAM_COMMUNITY_H
#define CORKDECK_STEAM_COMMUNITY_H
#include <stddef.h>
#include <stdint.h>
#define COMMUNITY_LIMIT (1024u * 1024u)
#define COMMUNITY_URL "https://raw.githubusercontent.com/lerosua/CorkDeck/refs/heads/codex/chinese-menu/docs/compat/reports.tsv"
extern const unsigned char steam_community_seed[];
extern const size_t steam_community_seed_size;
struct community_result {
    unsigned count;
    char stage[48], detail[384];
};
/* Validate the entire index before accepting a cache or network response. */
int community_lookup(const unsigned char *data, size_t size, uint32_t app, struct community_result *out);
unsigned char *community_read(const char *root, size_t *size);
int community_save(const char *root, const unsigned char *data, size_t size);
#endif
