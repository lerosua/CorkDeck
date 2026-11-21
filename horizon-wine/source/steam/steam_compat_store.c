#include "steam_compat.h"
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

static int fetch(CURL *curl, const char *url, struct response *r, uint32_t app, const char *stage)
{
    long http = 0;
    r->size = 0;
    if (!curl || !r->bytes) {
        network_diag("stage=steam.compat.%s appid=%u result=init_failed", stage, app);
        return 0;
    }
    curl_easy_reset(curl);
#define OPTION(k,v) do { CURLcode opt = curl_easy_setopt(curl, k, v); if (opt != CURLE_OK) { \
        network_diag("stage=steam.compat.%s appid=%u result=setup_failed curl=%d", stage, app, (int)opt); return 0; } } while (0)
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
    snprintf(context, sizeof(context), "steam.compat.%s appid=%u bytes=%zu", stage, app, r->size);
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
        int fetched = fetch(curl, url, &r, app, "windows");
        int ok = fetched && steam_compat_parse_minimum(r.bytes, r.size, app, &minimum);
        log_minimum(app, fetched, ok, minimum);
        SDL_LockMutex(store.mutex);
        if (r.generation != SDL_AtomicGet(&store.generation)) continue;
        store.view.minimum = minimum;
        store.view.windows = ok ? STORE_READY : STORE_UNAVAILABLE;
        SDL_UnlockMutex(store.mutex);
        wake_page();
        snprintf(url, sizeof(url), "https://store.steampowered.com/saleaction/ajaxgetdeckappcompatibilityreport?nAppID=%u", app);
        fetched = fetch(curl, url, &r, app, "deck");
        ok = fetched && steam_compat_parse_deck(r.bytes, r.size, app, &category);
        network_diag("stage=steam.compat.deck.parse appid=%u result=%s category=%d rating=\"%s\"",
                     app, !fetched ? "not_fetched" : ok ? "ok" : "invalid",
                     ok ? (int)category : -1, ok ? steam_compat_deck_rating(category) : "-");
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
