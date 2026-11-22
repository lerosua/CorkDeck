#ifndef CORKDECK_STEAM_ART_ZIP_H
#define CORKDECK_STEAM_ART_ZIP_H
#include "steam_manifest.h"
struct steam_art_entry;
struct steam_art_zip {
  struct steam_art_entry *entries;
  size_t count, capacity;
};
int steam_art_member(uint32_t app, uint32_t depot, const struct steam_manifest_file *file);
int steam_art_part(const char *cache, const char *name, char out[1024]);
int steam_art_add(struct steam_art_zip *art, const struct steam_manifest_file *file);
int steam_art_publish(const struct steam_art_zip *art, const char *cache, const char *stage,
                      int (*tick)(void *), void *context, unsigned char *buffer);
void steam_art_cleanup(const struct steam_art_zip *art, const char *cache);
#endif
