#ifndef CORKDECK_STEAM_NAMES_H
#define CORKDECK_STEAM_NAMES_H
#include <stdio.h>
#include <stdint.h>
struct steam_names;
int steam_names_collect(struct steam_names **map, const char *relative);
size_t steam_names_finish(struct steam_names *map);
void steam_names_free(struct steam_names *map);
int steam_names_path(const struct steam_names *map, const char *logical, char *disk, size_t cap);
const char *steam_names_original(const struct steam_names *map, const char *disk);
int steam_names_alias(const char *name);
int steam_names_write(const struct steam_names *map, FILE *marker);
int steam_names_read(struct steam_names **map, FILE *marker);
#endif
