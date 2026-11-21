/* Exercise routing on real worker threads with deterministic directory/CM responses. */
#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_cm_route.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char first[] = "old.steamserver.net:27017", second[] = "fast.steamserver.net:443";
static SDL_atomic_t launched_first, launched_second, discovery_calls, discarded;
static int cache_wins, cancel_all, bad_cert;
static Uint32 began;
struct steam_cm { struct steam_cm_route *route; };

void steam_discover(struct steam_probe_result *out, struct steam_servers *servers, steam_cancel_fn cancel, void *ctx)
{
    SDL_AtomicAdd(&discovery_calls, 1);
    Uint32 start = SDL_GetTicks();
    while (cache_wins && SDL_GetTicks() - start < 2500 && !cancel(ctx)) SDL_Delay(5);
    *out = (struct steam_probe_result){.code = STEAM_OK};
    servers->count = 3;
    strcpy(servers->endpoint[0], first);
    strcpy(servers->endpoint[1], first);
    strcpy(servers->endpoint[2], second);
}

struct steam_cm *steam_cm_dial(const char *endpoint, steam_cancel_fn cancel, void *ctx,
                               unsigned int *error, unsigned int *transport)
{
    if (!strcmp(endpoint, first)) {
        assert(!SDL_AtomicAdd(&launched_first, 1));
        if (!cache_wins && !bad_cert) {
            while (!cancel(ctx)) SDL_Delay(5);
            *error = 1;
            return NULL;
        }
    } else {
        assert(!strcmp(endpoint, second));
        assert(!SDL_AtomicAdd(&launched_second, 1));
        if (!bad_cert) assert(SDL_GetTicks() - began >= 750);
    }
    if (bad_cert) { *error = 260; *transport = 60; return NULL; }
    return calloc(1, sizeof(struct steam_cm));
}
void steam_cm_adopt(struct steam_cm *cm, steam_cancel_fn cancel, void *ctx, struct steam_cm_route *r)
{ (void)cancel; (void)ctx; cm->route = r; }
void steam_cm_close(struct steam_cm *cm)
{
    if (cm) { steam_cm_route_release(&cm->route, 1); free(cm); SDL_AtomicAdd(&discarded, 1); }
}
static int abort_connection(void *unused)
{ (void)unused; return cancel_all && SDL_GetTicks() - began > 100; }
static struct steam_cm *connect_again(const char *root, unsigned int *error)
{
    SDL_AtomicSet(&launched_first, 0);
    SDL_AtomicSet(&launched_second, 0);
    began = SDL_GetTicks();
    return steam_cm_connect(root, abort_connection, NULL, error);
}
int main(void)
{
    char root[] = "/tmp/cm-route-XXXXXX", path[256];
    assert(mkdtemp(root));
    snprintf(path, sizeof(path), "%s/steam-cm.endpoint", root);
    unsigned int error = 0;
    steam_cm_remember(root, first);
    cache_wins = 1;
    struct steam_cm *cm = connect_again(root, &error);
    assert(cm && !error && SDL_GetTicks()-began < 2000 && !SDL_AtomicGet(&launched_second));
    steam_cm_close(cm);
    assert(SDL_AtomicGet(&discovery_calls) == 1);

    cache_wins = 0;
    cm = connect_again(root, &error);
    assert(cm && !error && SDL_GetTicks()-began < 2500 && SDL_AtomicGet(&launched_second) == 1);
    steam_cm_close(cm);

    FILE *fp = fopen(path, "wb");
    assert(fp && fputs("not-steam.example:443", fp) >= 0 && !fclose(fp));
    bad_cert = 1;
    assert(!connect_again(root, &error) && error == 260 && steam_cm_connect_transport() == 60);
    assert(SDL_AtomicGet(&launched_first) == 1 && SDL_AtomicGet(&launched_second) == 1);
    bad_cert = 0;
    cancel_all = 1;
    assert(!connect_again(root, &error) && error == 1);
    assert(SDL_AtomicGet(&discarded) == 2);
    assert(!unlink(path) && !rmdir(root));
    puts("Cached CM wins while discovery is pending; a slow CM is overtaken, duplicate and invalid endpoints are skipped, cancellation and verification errors propagate");
}
