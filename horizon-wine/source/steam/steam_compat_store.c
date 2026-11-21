#include "steam_compat.h"
#include "../network_diag.h"
#include <SDL.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

/* One worker for the detail page; a new selection supersedes the old lookup.
 * The public store does not need a Steam session, cookie or publisher key. */
static struct {
    SDL_mutex *mutex;
    SDL_cond *changed;
    SDL_Thread *thread;
    SDL_atomic_t generation;
    uint32_t app;
    struct steam_compat_view view;
    int stop;
} store;
#define STORE_LIMIT (1024u * 1024u)

struct response {
    char *bytes;
    size_t size;
    int generation;
};

static size_t receive(char *data, size_t width, size_t count, void *opaque)
{
    struct response *r = opaque;
    if (width && count > (STORE_LIMIT - r->size) / width) return 0;
    size_t length = width * count;
    memcpy(r->bytes + r->size, data, length);
    r->size += length;
    return length;
}

static int superseded(void *opaque, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    struct response *r = opaque;
    (void)a; (void)b; (void)c; (void)d;
    return r->generation != SDL_AtomicGet(&store.generation);
}

static int fetch(CURL *curl, const char *url, struct response *r, const char *stage)
{
    long http = 0;
    r->size = 0;
    if (!curl || !r->bytes) return 0;
    curl_easy_reset(curl);
#define OPTION(k,v) do { if (curl_easy_setopt(curl, k, v) != CURLE_OK) return 0; } while (0)
    OPTION(CURLOPT_URL, url);
    OPTION(CURLOPT_PROXY, "");
    OPTION(CURLOPT_ACCEPT_ENCODING, "");
    OPTION(CURLOPT_NOSIGNAL, 1L);
    OPTION(CURLOPT_CONNECTTIMEOUT, 5L);
    OPTION(CURLOPT_TIMEOUT, 12L);
    OPTION(CURLOPT_WRITEFUNCTION, receive);
    OPTION(CURLOPT_WRITEDATA, r);
    OPTION(CURLOPT_NOPROGRESS, 0L);
    OPTION(CURLOPT_XFERINFOFUNCTION, superseded);
    OPTION(CURLOPT_XFERINFODATA, r);
#undef OPTION
    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    network_diag("stage=steam.compat.%s curl=%d http=%ld bytes=%zu", stage, (int)code, http, r->size);
    return code == CURLE_OK && http == 200;
}

/* ui_wait sleeps until an SDL event when the page is idle. */
static void wake_page(void)
{
    SDL_Event event = {.type = SDL_USEREVENT};
    SDL_PushEvent(&event);
}

static int run(void *unused)
{
    (void)unused;
    CURL *curl = curl_easy_init();
    struct response r = {.bytes = malloc(STORE_LIMIT)};
    SDL_LockMutex(store.mutex);
    while (!store.stop) {
        if (r.generation == SDL_AtomicGet(&store.generation)) {
            SDL_CondWait(store.changed, store.mutex);
            continue;
        }
        r.generation = SDL_AtomicGet(&store.generation);
        uint32_t app = store.app;
        struct steam_minimum minimum = {0};
        unsigned int category = 0;
        char url[160];
        SDL_UnlockMutex(store.mutex);
        snprintf(url, sizeof(url), "https://store.steampowered.com/api/appdetails?appids=%u&l=english", app);
        int ok = fetch(curl, url, &r, "windows") && steam_compat_parse_minimum(r.bytes, r.size, app, &minimum);
        SDL_LockMutex(store.mutex);
        if (r.generation != SDL_AtomicGet(&store.generation)) continue;
        store.view.minimum = minimum;
        store.view.windows = ok ? STORE_READY : STORE_UNAVAILABLE;
        SDL_UnlockMutex(store.mutex);
        wake_page();
        snprintf(url, sizeof(url), "https://store.steampowered.com/saleaction/ajaxgetdeckappcompatibilityreport?nAppID=%u", app);
        ok = fetch(curl, url, &r, "deck") && steam_compat_parse_deck(r.bytes, r.size, app, &category);
        SDL_LockMutex(store.mutex);
        if (r.generation != SDL_AtomicGet(&store.generation)) continue;
        store.view.deck_category = category;
        store.view.deck = ok ? STORE_READY : STORE_UNAVAILABLE;
        SDL_UnlockMutex(store.mutex);
        wake_page();
        SDL_LockMutex(store.mutex);
    }
    SDL_UnlockMutex(store.mutex);
    if (curl) curl_easy_cleanup(curl);
    free(r.bytes);
    return 0;
}

void steam_compat_shutdown(void)
{
    if (store.thread) {
        SDL_LockMutex(store.mutex);
        store.stop = 1;
        SDL_AtomicAdd(&store.generation, 1);
        SDL_CondSignal(store.changed);
        SDL_UnlockMutex(store.mutex);
        SDL_WaitThread(store.thread, NULL);
    }
    if (store.changed) SDL_DestroyCond(store.changed);
    if (store.mutex) SDL_DestroyMutex(store.mutex);
    memset(&store, 0, sizeof(store));
}

void steam_compat_request(uint32_t app, int retry)
{
    if (!app) return;
    if (!store.thread) {
        store.mutex = SDL_CreateMutex();
        store.changed = SDL_CreateCond();
        if (store.mutex && store.changed)
            store.thread = SDL_CreateThreadWithStackSize(run, "steam-store", 192u*1024u, NULL);
        if (!store.thread) { steam_compat_shutdown(); return; }
    }
    SDL_LockMutex(store.mutex);
    if (app != store.app || (retry && store.view.deck != STORE_PENDING)) {
        store.app = app;
        store.view = (struct steam_compat_view){0};
        SDL_AtomicAdd(&store.generation, 1);
        SDL_CondSignal(store.changed);
    }
    SDL_UnlockMutex(store.mutex);
}

void steam_compat_status(uint32_t app, struct steam_compat_view *view)
{
    *view = (struct steam_compat_view){.windows = STORE_UNAVAILABLE, .deck = STORE_UNAVAILABLE};
    if (!store.thread) return;
    SDL_LockMutex(store.mutex);
    if (store.app == app) *view = store.view;
    SDL_UnlockMutex(store.mutex);
}
