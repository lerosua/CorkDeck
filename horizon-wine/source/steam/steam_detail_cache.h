#ifndef CORKDECK_STEAM_DETAIL_CACHE_H
#define CORKDECK_STEAM_DETAIL_CACHE_H
#include <stddef.h>
#include <stdint.h>
/* NULL language selects public Windows requirements. Installation estimates
 * are specific to an account and its selected game language. */
int steam_detail_read(const char *root, uint32_t app, uint64_t account, const char *language,
                      void *data, size_t size);
int steam_detail_write(const char *root, uint32_t app, uint64_t account, const char *language,
                       const void *data, size_t size);
#endif
