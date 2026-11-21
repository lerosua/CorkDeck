#define _POSIX_C_SOURCE 200809L
#include "steam_cm_route.h"
#include "steam_file.h"
#include "../network_diag.h"
#include <SDL.h>
#include <curl/curl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { STAGGER_MS = 750, DIALERS = 2 };
struct dial {
    SDL_Thread *thread;
    SDL_atomic_t done;
    SDL_atomic_t *stop;
    char endpoint[128];
    const char *source;
    struct steam_cm *cm;
    unsigned int error, transport;
};
struct steam_cm_route {
    struct dial dial[DIALERS];
    SDL_atomic_t stop_dials, stop_directory, directory_done;
    SDL_Thread *directory;
    struct steam_servers servers;
    struct steam_probe_result discovery;
};

static _Thread_local unsigned int last_transport;
unsigned int steam_cm_connect_transport(void) { return last_transport; }

static int cache_path(const char *root, char path[512])
{
    if (!root || !*root) return 0;
    int n = snprintf(path, 512, "%s/steam-cm.endpoint", root);
    return n > 0 && n < 512;
}
static int cached_endpoint(const char *root, char out[128])
{
    char path[512], data[128];
    if (!cache_path(root, path) || !steam_file_recover(path)) return 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    size_t n = fread(data, 1, sizeof(data), fp);
    int ok = n && n < sizeof(data) && !ferror(fp) && !memchr(data, 0, n);
    fclose(fp);
    if (!ok) return 0;
    data[n] = 0;
    if (!steam_endpoint_valid(data)) return 0;
    strcpy(out, data);
    return 1;
}
void steam_cm_remember(const char *root, const char *endpoint)
{
    char path[512], tmp[520], old[128];
    if (!endpoint || !steam_endpoint_valid(endpoint) || !cache_path(root, path)) return;
    if (cached_endpoint(root, old) && !strcmp(old, endpoint)) return;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    size_t n = strlen(endpoint);
    int ok = fd >= 0 && write(fd, endpoint, n) == (ssize_t)n && fsync(fd) == 0;
    if (fd >= 0 && close(fd)) ok = 0;
    if (ok) ok = steam_file_replace(tmp, path);
    if (!ok) unlink(tmp);
    network_diag("stage=steam.cm.cache endpoint=%s result=%s", endpoint, ok ? "saved" : "write_failed");
}

static int stopped(void *flag) { return SDL_AtomicGet(flag); }

static int discover(void *opaque)
{
    struct steam_cm_route *r = opaque;
    for (unsigned int attempt = 1; attempt <= 3; attempt++) {
        Uint32 start = SDL_GetTicks();
        steam_discover(&r->discovery, &r->servers, stopped, &r->stop_directory);
        network_diag("stage=steam.discovery attempt=%u result=%u servers=%u elapsed_ms=%u",
                     attempt, (unsigned)r->discovery.code, r->servers.count, SDL_GetTicks() - start);
        if (r->discovery.code != STEAM_NETWORK || attempt == 3 || stopped(&r->stop_dials)) break;
        start = SDL_GetTicks();
        while (SDL_GetTicks() - start < 1500 * attempt && !stopped(&r->stop_dials)) SDL_Delay(20);
        if (stopped(&r->stop_dials)) break;
    }
    SDL_AtomicSet(&r->directory_done, 1);
    return 0;
}
static void dial_log(const struct dial *d, const char *result, Uint32 elapsed)
{
    const char *colon = strchr(d->endpoint, ':');
    network_diag("stage=steam.cm.node host=%.*s port=%s source=%s result=%s elapsed_ms=%u error=%u transport=%u",
                 (int)(colon - d->endpoint), d->endpoint, colon + 1, d->source,
                 result, elapsed, d->error, d->transport);
}
static int dial_worker(void *opaque)
{
    struct dial *d = opaque;
    Uint32 start = SDL_GetTicks();
    dial_log(d, "start", 0);
    d->cm = steam_cm_dial(d->endpoint, stopped, d->stop, &d->error, &d->transport);
    dial_log(d, stopped(d->stop) ? "cancelled" : d->cm ? "ready" : "failed", SDL_GetTicks() - start);
    SDL_AtomicSet(&d->done, 1);
    return 0;
}

/* Only the session thread owns the route. After a winner, it can log on
 * immediately while the losers unwind and an in-flight directory read finishes. */
void steam_cm_route_release(struct steam_cm_route **route, int closing)
{
    struct steam_cm_route *r = *route;
    if (!r) return;
    if (closing) {
        SDL_AtomicSet(&r->stop_dials, 1);
        SDL_AtomicSet(&r->stop_directory, 1);
    }
    int pending = 0;
    for (unsigned int i = 0; i < DIALERS; i++) {
        struct dial *d = &r->dial[i];
        if (!d->thread) continue;
        if (!closing && !stopped(&d->done)) { pending = 1; continue; }
        SDL_WaitThread(d->thread, NULL);
        d->thread = NULL;
        steam_cm_close(d->cm);
    }
    if (r->directory) {
        if (!closing && !stopped(&r->directory_done)) return;
        SDL_WaitThread(r->directory, NULL);
        r->directory = NULL;
    }
    if (!pending) { free(r); *route = NULL; }
}

struct steam_cm *steam_cm_connect(const char *root, steam_cancel_fn cancel, void *context,
                                  unsigned int *error)
{
    struct steam_cm_route *r = calloc(1, sizeof(*r));
    char queue[STEAM_CM_SERVERS + 1][128];
    unsigned int next = 0, count = 0;
    Uint32 launched = 0;
    *error = 5;
    last_transport = 0;
    if (!r) return NULL;
    int cached = cached_endpoint(root, queue[0]);
    count = cached ? 1 : 0;
    network_diag("stage=steam.cm.cache result=%s", cached ? "hit" : "miss");

    /* Initialize curl on the coordinator before starting concurrent easy handles. */
    CURL *init = curl_easy_init();
    if (!init) { free(r); return NULL; }
    curl_easy_cleanup(init);
    r->directory = SDL_CreateThreadWithStackSize(discover, "steam-directory", 128*1024, r);
    if (!r->directory) r->discovery.code = STEAM_MEMORY;
    for (;;) {
        if (cancel && cancel(context)) { *error = 1; break; }
        if (r->directory && stopped(&r->directory_done)) {
            SDL_WaitThread(r->directory, NULL);
            r->directory = NULL;
            if (r->discovery.code == STEAM_OK) {
                for (unsigned int i = 0; i < r->servers.count; i++) {
                    unsigned int j = 0;
                    while (j < count && strcmp(r->servers.endpoint[i], queue[j])) j++;
                    if (j == count) strcpy(queue[count++], r->servers.endpoint[i]);
                }
            }
        }
        struct dial *idle = NULL;
        unsigned int active = 0;
        for (unsigned int i = 0; i < DIALERS; i++) {
            struct dial *d = &r->dial[i];
            if (d->thread && stopped(&d->done)) {
                SDL_WaitThread(d->thread, NULL);
                d->thread = NULL;
                *error = d->error;
                last_transport = d->transport;
                if (d->cm) {
                    struct steam_cm *winner = d->cm;
                    d->cm = NULL;
                    SDL_AtomicSet(&r->stop_dials, 1);
                    steam_cm_adopt(winner, cancel, context, r);
                    network_diag("stage=steam.cm.selected endpoint=%s source=%s", d->endpoint, d->source);
                    return winner;
                }
            }
            if (d->thread) active++;
            else if (!idle) idle = d;
        }
        if (idle && next < count && (!active || SDL_GetTicks() - launched >= STAGGER_MS)) {
            *idle = (struct dial){.source = cached && !next ? "cache" : "directory", .stop = &r->stop_dials};
            strcpy(idle->endpoint, queue[next++]);
            launched = SDL_GetTicks();
            idle->thread = SDL_CreateThreadWithStackSize(dial_worker, "steam-cm-dial", 192*1024, idle);
            if (!idle->thread) { idle->error = *error = 5; dial_log(idle, "thread_failed", 0); }
            continue;
        }
        if (!active && next == count && !r->directory) break;
        SDL_Delay(10);
    }
    /* A stale cache entry must not mask a transient directory failure. */
    if (*error != 1 && (!count || r->discovery.code == STEAM_NETWORK)) {
        *error = 100 + r->discovery.code;
        last_transport = r->discovery.transport_code;
    }
    steam_cm_route_release(&r, 1);
    return NULL;
}
