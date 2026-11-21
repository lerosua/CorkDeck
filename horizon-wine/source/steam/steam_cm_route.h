#ifndef CORKDECK_STEAM_CM_ROUTE_H
#define CORKDECK_STEAM_CM_ROUTE_H
#include "steam_cm.h"

struct steam_cm_route;
struct steam_cm *steam_cm_dial(const char *endpoint, steam_cancel_fn cancel, void *context,
                              unsigned int *error, unsigned int *transport);
void steam_cm_adopt(struct steam_cm *, steam_cancel_fn, void *, struct steam_cm_route *);
void steam_cm_route_release(struct steam_cm_route **, int closing);
#endif
