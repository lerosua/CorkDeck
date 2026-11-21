#include "steam_compat.h"
#include "steam_detail_cache.h"
#include "../network_diag_curl.h"
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
    char root[256];
    int stop, fetching, sync_catalog;
    Uint32 last_sync;
    unsigned char *catalog;
    size_t catalog_size;
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

static int fetch(CURL *curl, const char *url, struct response *r, uint32_t app, const char *stage)
{
    long http = 0;
    r->size = 0;
    if (!curl || !r->bytes) {
        network_diag("stage=%s appid=%u result=init_failed", stage, app);
        return 0;
    }
    curl_easy_reset(curl);
#define OPTION(k,v) do { CURLcode opt = curl_easy_setopt(curl, k, v); if (opt != CURLE_OK) { \
        network_diag("stage=%s appid=%u result=setup_failed curl=%d", stage, app, (int)opt); return 0; } } while (0)
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
    char context[96];
    snprintf(context, sizeof(context), "%s appid=%u bytes=%zu", stage, app, r->size);
    network_diag_curl(context, curl, code);
    return code == CURLE_OK && http == 200;
}

/* Publisher-supplied specs are public. Keep each field on a single line
 * and omit links instead of copying URLs into diagnostic exports. */
static const char *log_field(char *field)
{
    if (!*field) return "-";
    if (strstr(field, "://") || strchr(field, '?')) return "(link omitted)";
    for (char *p = field; *p; ++p)
        if ((unsigned char)*p < 32 || *p == 127 || *p == '"') *p = ' ';
    return field;
}

static void log_minimum(uint32_t app, int fetched, int parsed, struct steam_minimum m)
{
    const char *result = !fetched ? "not_fetched" : !parsed ? "invalid" :
        (m.memory[0] || m.directx[0] || m.processor[0] || m.graphics[0]) ? "ok" : "no_fields";
    network_diag("stage=steam.compat.windows.parse appid=%u result=%s ram=\"%s\" directx=\"%s\"",
                 app, result, log_field(m.memory), log_field(m.directx));
    if (parsed)
        network_diag("stage=steam.compat.windows.hardware appid=%u cpu=\"%s\" gpu=\"%s\"",
                     app, log_field(m.processor), log_field(m.graphics));
}

/* ui_wait sleeps until an SDL event when the page is idle. */
static void wake_page(void)
{
    SDL_Event event = {.type = SDL_USEREVENT};
    SDL_PushEvent(&event);
}

static void lookup_community(void)
{
    community_lookup(store.catalog ? store.catalog : steam_community_seed,
                     store.catalog ? store.catalog_size : steam_community_seed_size,
                     store.app, &store.view.community);
}

static int run(void *unused)
{
    (void)unused;
    CURL *curl = curl_easy_init();
    struct response r = {.bytes = malloc(STORE_LIMIT)};
    SDL_LockMutex(store.mutex);
    while (!store.stop) {
        if (!store.fetching && !store.sync_catalog) {
            SDL_CondWait(store.changed, store.mutex);
            continue;
        }
        r.generation = SDL_AtomicGet(&store.generation);
        uint32_t app = store.app;
        char root[sizeof(store.root)];
        strcpy(root, store.root);
        struct steam_minimum minimum = {0};
        char url[160];
        if (store.sync_catalog) {
            SDL_UnlockMutex(store.mutex);
            int fetched = fetch(curl, COMMUNITY_URL, &r, app, "steam.compat.community");
            struct community_result result;
            int valid = fetched && community_lookup((unsigned char *)r.bytes, r.size, app, &result);
            SDL_LockMutex(store.mutex);
            if (r.generation != SDL_AtomicGet(&store.generation)) continue;
            store.sync_catalog = 0;
            store.last_sync = SDL_GetTicks();
            if (valid) {
                int saved = community_save(root, (unsigned char *)r.bytes, r.size);
                unsigned char *copy = malloc(r.size);
                if (copy) {
                    memcpy(copy, r.bytes, r.size);
                    free(store.catalog);
                    store.catalog = copy;
                    store.catalog_size = r.size;
                    store.view.community = result;
                }
                network_diag("stage=steam.compat.community cache=%s appid=%u reports=%u",
                             saved ? "saved" : "write_failed", app, result.count);
            } else {
                network_diag("stage=steam.compat.community cache=kept result=%s",
                             fetched ? "invalid" : "offline_or_unavailable");
            }
            wake_page();
            continue;
        }
        SDL_UnlockMutex(store.mutex);
        snprintf(url, sizeof(url), "https://store.steampowered.com/api/appdetails?appids=%u&l=english", app);
        int fetched = fetch(curl, url, &r, app, "steam.compat.windows");
        int ok = fetched && steam_compat_parse_minimum(r.bytes, r.size, app, &minimum);
        log_minimum(app, fetched, ok, minimum);
        SDL_LockMutex(store.mutex);
        if (r.generation != SDL_AtomicGet(&store.generation)) continue;
        store.fetching = 0;
        if (ok && (store.view.windows != STORE_READY || minimum.memory[0] || minimum.processor[0] ||
                   minimum.graphics[0] || minimum.directx[0])) {
            store.view.minimum = minimum;
            store.view.windows = STORE_READY;
            int saved = steam_detail_write(root, app, 0, NULL, &minimum, sizeof(minimum));
            network_diag("stage=steam.details.windows appid=%u cache=%s", app, saved ? "saved" : "write_failed");
        } else if (store.view.windows != STORE_READY) {
            store.view.windows = STORE_UNAVAILABLE;
        }
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
    free(store.catalog);
    memset(&store, 0, sizeof(store));
}

void steam_compat_request(const char *root, uint32_t app, int retry)
{
    if (!app || !root || strlen(root) >= sizeof(store.root)) return;
    if (!store.thread) {
        store.mutex = SDL_CreateMutex();
        store.changed = SDL_CreateCond();
        if (store.mutex && store.changed)
            store.thread = SDL_CreateThreadWithStackSize(run, "steam-store", 192u*1024u, NULL);
        if (!store.thread) { steam_compat_shutdown(); return; }
    }
    SDL_LockMutex(store.mutex);
    if (app != store.app || strcmp(store.root, root)) {
        SDL_AtomicAdd(&store.generation, 1);
        if (strcmp(store.root, root)) {
            free(store.catalog);
            store.catalog = community_read(root, &store.catalog_size);
            network_diag("stage=steam.compat.community source=%s", store.catalog ? "sd" : "bundled");
            store.sync_catalog = 1;
        }
        store.app = app;
        strcpy(store.root, root);
        store.fetching = 0;
        store.view = (struct steam_compat_view){0};
        lookup_community();
        struct steam_minimum m;
        if (steam_detail_read(root, app, 0, NULL, &m, sizeof(m)) &&
            memchr(m.processor, 0, sizeof(m.processor)) && memchr(m.memory, 0, sizeof(m.memory)) &&
            memchr(m.graphics, 0, sizeof(m.graphics)) && memchr(m.directx, 0, sizeof(m.directx))) {
            store.view.minimum = m;
            store.view.windows = STORE_READY;
        }
        network_diag("stage=steam.details.windows appid=%u cache=%s", app,
                     store.view.windows == STORE_READY ? "hit" : "miss");
    }
    if (retry || SDL_GetTicks() - store.last_sync >= 30u * 60u * 1000u)
        store.sync_catalog = 1;
    if (!store.fetching && (retry || store.view.windows != STORE_READY)) {
        store.fetching = 1;
        if (store.view.windows != STORE_READY) store.view.windows = STORE_PENDING;
        SDL_AtomicAdd(&store.generation, 1);
    }
    SDL_CondSignal(store.changed);
    SDL_UnlockMutex(store.mutex);
}

void steam_compat_status(uint32_t app, struct steam_compat_view *view)
{
    *view = (struct steam_compat_view){.windows = STORE_UNAVAILABLE};
    if (!store.thread) return;
    SDL_LockMutex(store.mutex);
    if (store.app == app) *view = store.view;
    SDL_UnlockMutex(store.mutex);
}
