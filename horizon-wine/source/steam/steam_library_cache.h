#ifndef CORKDECK_STEAM_LIBRARY_CACHE_H
#define CORKDECK_STEAM_LIBRARY_CACHE_H

#include "steam_library.h"
#include <time.h>

struct steam_library_stamp {
    uint64_t synced;
    char language[32];
};
int steam_library_fresh(const struct steam_library_stamp *, const char *language, time_t now);
int steam_library_cache_load(const char *root, uint64_t account, struct steam_library *, struct steam_library_stamp *);
int steam_library_cache_save(const char *root, const struct steam_library *, const struct steam_library_stamp *);
void steam_library_cache_forget(const char *root);
#endif
