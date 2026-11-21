#ifndef CORKDECK_STEAM_COMPAT_H
#define CORKDECK_STEAM_COMPAT_H

#include "steam_community.h"
#include <stddef.h>
#include <stdint.h>

enum store_state { STORE_PENDING, STORE_READY, STORE_UNAVAILABLE };
struct steam_minimum {
    char memory[72], graphics[144], directx[48], processor[112];
};
struct steam_compat_view {
    enum store_state windows;
    struct steam_minimum minimum;
    struct community_result community;
};

enum steam_fit { FIT_UNKNOWN, FIT_PROMISING, FIT_TIGHT, FIT_HIGH_RISK };
struct steam_assessment {
    enum steam_fit fit;
    const char *reason;
};
struct steam_assessment steam_compat_estimate(const struct steam_minimum *minimum);
int steam_compat_parse_minimum(const char *json, size_t size, uint32_t app, struct steam_minimum *minimum);
void steam_compat_request(const char *root, uint32_t app, int retry);
void steam_compat_status(uint32_t app, struct steam_compat_view *view);
void steam_compat_shutdown(void);

#endif
