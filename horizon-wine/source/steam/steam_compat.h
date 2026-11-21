#ifndef CORKDECK_STEAM_COMPAT_H
#define CORKDECK_STEAM_COMPAT_H

#include <stddef.h>
#include <stdint.h>

enum store_state { STORE_PENDING, STORE_READY, STORE_UNAVAILABLE };
struct steam_minimum {
    char memory[72], graphics[144], directx[48], processor[112];
};
struct steam_compat_view {
    enum store_state windows, deck;
    struct steam_minimum minimum;
    unsigned int deck_category;
};

const char *steam_compat_record(uint32_t app);
const char *steam_compat_estimate(const struct steam_minimum *minimum);
int steam_compat_parse_minimum(const char *json, size_t size, uint32_t app, struct steam_minimum *minimum);
const char *steam_compat_deck_rating(unsigned int category);
int steam_compat_parse_deck(const char *json, size_t size, uint32_t app, unsigned int *category);
void steam_compat_request(uint32_t app, int retry);
void steam_compat_status(uint32_t app, struct steam_compat_view *view);
void steam_compat_shutdown(void);

#endif
