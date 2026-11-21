#define _POSIX_C_SOURCE 200809L
#include "steam_artwork.h"
#include "steam_settings.h"
#include "../json_reader.h"
#include "../launcher_image.h"
#include "../network_diag_curl.h"
#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* Two rows of seven visible cards, plus room for an in-flight request while paging. */
#define ART_SLOTS 16
#define ART_BYTES (2u * 1024u * 1024u)
enum cover_state { EMPTY, CHECK_CACHE, LOADING, READY, FAILED, NEED_DOWNLOAD };
struct cover {
  uint32_t appid;
  unsigned int use;
  Uint32 retry_at, retry_delay;
  enum cover_state state;
  int width, height;
  struct launcher_icon decoded;
  SDL_Texture *texture;
};
struct steam_artwork {
  SDL_Thread *thread;
  SDL_mutex *mutex;
  SDL_cond *condition;
  SDL_atomic_t stop, paused;
  struct cover covers[ART_SLOTS];
  unsigned int use;
  char language[32];
  char cache[512];
  uint64_t cache_bytes;
  unsigned int cache_count;
};
struct response {
  struct steam_artwork *owner;
  unsigned char *data;
  size_t size;
  int too_large;
};
static int dir(const char *path) {
  struct stat st;
  if (!lstat(path, &st))
    return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
  return errno == ENOENT && !mkdir(path, 0777);
}
static int regular(const char *path) {
  struct stat st;
  return !lstat(path, &st) && S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) &&
         st.st_size > 0 && st.st_size <= ART_BYTES;
}
static const char *save_cover(struct steam_artwork *p, const char *path,
                              const struct response *r, int *write_errno)
{
  char temporary[600];
  struct stat st;
  if (!*path) return "cache_unavailable";
  if (!lstat(path, &st)) return "already_exists";
  if (errno != ENOENT) { *write_errno = errno; return "stat_failed"; }
  if (p->cache_bytes + r->size > 64u * 1024u * 1024u || p->cache_count >= 512)
    return "cache_limit";
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0) { *write_errno = errno; return "open_failed"; }
  FILE *file = fdopen(fd, "wb");
  const char *result = "write_failed";
  if (!file) {
    *write_errno = errno;
    close(fd);
  } else {
    if (fwrite(r->data, 1, r->size, file) != r->size)
      *write_errno = errno ? errno : EIO;
    if (fclose(file) && !*write_errno) { *write_errno = errno; result = "close_failed"; }
    if (!*write_errno) {
      if (!rename(temporary, path)) {
        p->cache_bytes += r->size;
        ++p->cache_count;
        return "saved";
      }
      *write_errno = errno;
      result = "rename_failed";
    }
  }
  unlink(temporary);
  return result;
}

static size_t receive(char *data, size_t size, size_t count, void *opaque) {
  struct response *r = opaque;
  size_t n;
  if (size && count > SIZE_MAX / size)
    return 0;
  n = size * count;
  if (n > ART_BYTES - r->size) {
    r->too_large = 1;
    return 0;
  }
  memcpy(r->data + r->size, data, n);
  r->size += n;
  return n;
}
static int cancelled(void *opaque, curl_off_t a, curl_off_t b, curl_off_t c,
                     curl_off_t d) {
  struct response *r = opaque;
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  return SDL_AtomicGet(&r->owner->stop) || SDL_AtomicGet(&r->owner->paused);
}
/* Only Steam-owned HTTPS CDN authorities may come from store JSON. */
static int store_header(const struct response *r, uint32_t appid, char *url, size_t capacity)
{
  struct parser root = {r->data, r->data + r->size, 0}, entry, field, data;
  int success;
  char id[16];
  snprintf(id, sizeof(id), "%u", appid);
  if (!json_member(root, id, &entry) || !json_member(entry, "success", &field) ||
      !json_bool(&field, &success) || !success || !json_member(entry, "data", &data) ||
      !json_member(data, "header_image", &field) || !json_string(&field, url, capacity) ||
      strncmp(url, "https://", 8)) return 0;
  const char *host = url + 8;
  size_t n = strspn(host, "abcdefghijklmnopqrstuvwxyz0123456789.-");
  const char suffix[] = ".steamstatic.com";
  return n > sizeof(suffix)-1 && host[n] == '/' &&
         !memcmp(host + n - (sizeof(suffix)-1), suffix, sizeof(suffix)-1) &&
         !strpbrk(url, "\r\n\t\\ ");
}

static int fetch(CURL *curl, struct response *r, uint32_t appid, const char *kind,
                 const char *url, int *transient)
{
  long http = 0;
  char context[180];
  CURLcode code;
  r->size = 0;
  r->too_large = 0;
  curl_easy_reset(curl);
#define OPT(k, v) do {                         \
    code = curl_easy_setopt(curl, k, v);        \
    if (code != CURLE_OK) goto request_done;    \
  } while (0)
  OPT(CURLOPT_URL, url);
  OPT(CURLOPT_PROXY, "");
  OPT(CURLOPT_ACCEPT_ENCODING, "");
  OPT(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
  OPT(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
  OPT(CURLOPT_FOLLOWLOCATION, 0L);
  OPT(CURLOPT_SSL_VERIFYPEER, 1L);
  OPT(CURLOPT_SSL_VERIFYHOST, 2L);
  OPT(CURLOPT_NOSIGNAL, 1L);
  OPT(CURLOPT_CONNECTTIMEOUT, 5L);
  OPT(CURLOPT_TIMEOUT, 15L);
  OPT(CURLOPT_WRITEFUNCTION, receive);
  OPT(CURLOPT_WRITEDATA, r);
  OPT(CURLOPT_NOPROGRESS, 0L);
  OPT(CURLOPT_XFERINFOFUNCTION, cancelled);
  OPT(CURLOPT_XFERINFODATA, r);
  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
request_done:
  snprintf(context, sizeof(context), "steam.artwork.request appid=%u kind=%s host=%.*s bytes=%zu limit_hit=%d",
           appid, kind, (int)strcspn(url + 8, "/"), url + 8, r->size, r->too_large);
  network_diag_curl(context, curl, code);
  if ((!r->too_large && code != CURLE_OK && code != CURLE_ABORTED_BY_CALLBACK) ||
      http == 408 || http == 429 || http >= 500) *transient = 1;
  curl_easy_reset(curl);
  return code == CURLE_OK && http == 200 && r->size;
#undef OPT
}

static int download_cover(CURL *curl, struct response *r, uint32_t appid, const char *path,
                          struct launcher_icon *icon, int *transient)
{
  char url[2048];
  static const char *const kinds[] = {"portrait", "store_header", "header"};
  for (unsigned int i = 0; i < 3; ++i) {
    if (cancelled(r, 0, 0, 0, 0)) break;
    if (i == 1) {
      snprintf(url, sizeof(url), "https://store.steampowered.com/api/appdetails?appids=%u&filters=basic&l=%s",
               appid, r->owner->language);
      if (!fetch(curl, r, appid, "store_lookup", url, transient)) continue;
      int found = store_header(r, appid, url, sizeof(url));
      network_diag("stage=steam.artwork.store appid=%u result=%s", appid, found ? "found" : "no_header");
      if (!found || cancelled(r, 0, 0, 0, 0)) continue;
    } else {
      snprintf(url, sizeof(url), "https://%s/store_item_assets/steam/apps/%u/%s",
               i == 0 ? "shared.akamai.steamstatic.com" : "shared.fastly.steamstatic.com",
               appid, i == 0 ? "library_600x900.jpg" : "header.jpg");
    }
    if (!fetch(curl, r, appid, kinds[i], url, transient)) continue;
    int loaded = launcher_image_decode(r->data, r->size, icon);
    network_diag("stage=steam.artwork.decode appid=%u kind=%s result=%s width=%d height=%d",
                 appid, kinds[i], loaded ? "ok" : "failed", icon->width, icon->height);
    if (!loaded) continue;
    int write_errno = 0;
    const char *result = save_cover(r->owner, path, r, &write_errno);
    network_diag("stage=steam.artwork.write appid=%u kind=%s result=%s errno=%d", appid, kinds[i], result, write_errno);
    return 1;
  }
  return 0;
}

static int run(void *opaque) {
  struct steam_artwork *p = opaque;
  CURL *curl = curl_easy_init();
  unsigned char *data = malloc(ART_BYTES);
  if (!curl || !data) {
    network_diag("stage=steam.artwork.worker result=init_failed");
    goto done;
  }
  unsigned int cursor = 0;
  while (!SDL_AtomicGet(&p->stop)) {
    unsigned int index;
    uint32_t appid = 0;
    char path[560];
    struct launcher_icon icon = {0};
    int loaded = 0, cache_only = 0;
    SDL_LockMutex(p->mutex);
    for (;;) {
      if (SDL_AtomicGet(&p->stop))
        break;
      /* Check queued cards on SD before starting another network transfer. */
      for (index = 0; index < ART_SLOTS; ++index)
        if (p->covers[index].state == CHECK_CACHE) break;
      if (index == ART_SLOTS && !SDL_AtomicGet(&p->paused))
        for (unsigned int n = 0; n < ART_SLOTS; ++n) {
          unsigned int slot = (cursor + n) % ART_SLOTS;
          if (p->covers[slot].state == NEED_DOWNLOAD) {
            index = slot;
            cursor = (slot + 1) % ART_SLOTS;
            break;
          }
        }
      if (index < ART_SLOTS) {
        cache_only = p->covers[index].state == CHECK_CACHE;
        appid = p->covers[index].appid;
        p->covers[index].state = LOADING;
      }
      if (appid)
        break;
      SDL_CondWaitTimeout(p->condition, p->mutex, 100);
    }
    SDL_UnlockMutex(p->mutex);
    if (!appid)
      break;
    path[0] = 0;
    if (p->cache[0])
      snprintf(path, sizeof(path), "%s/%u.jpg", p->cache, appid);
    if (cache_only && path[0]) {
      const int cached = regular(path);
      if (cached) loaded = launcher_image_load(path, &icon);
      const char *source = "current";
      int present = cached;
      if (!loaded) {
        /* The installed-game page stores its portrait under this older name. */
        char portrait[560];
        snprintf(portrait, sizeof(portrait), "%s/corkdeck-%u-cover.jpg", p->cache, appid);
        if (regular(portrait)) {
          source = "legacy";
          present = 1;
          loaded = launcher_image_load(portrait, &icon);
        }
      }
      network_diag("stage=steam.artwork.cache appid=%u source=%s result=%s", appid, source,
                   loaded ? "loaded" : present ? "load_failed" : "miss");
    } else if (cache_only) {
      network_diag("stage=steam.artwork.cache appid=%u result=unavailable", appid);
    }
    int transient = 0;
    if (!cache_only && !loaded) {
      struct response r = {.owner = p, .data = data};
      loaded = download_cover(curl, &r, appid, path, &icon, &transient);
    }
    if (loaded && !launcher_icon_fit(&icon, 384)) {
      network_diag("stage=steam.artwork.resize appid=%u result=failed", appid);
      launcher_icon_free(&icon);
      loaded = 0;
    }
    SDL_LockMutex(p->mutex);
    if (p->covers[index].appid == appid) {
      if (loaded) {
        p->covers[index].decoded = icon;
        memset(&icon, 0, sizeof(icon));
        p->covers[index].state = READY;
      } else if (cache_only || SDL_AtomicGet(&p->paused)) {
        p->covers[index].state = NEED_DOWNLOAD;
      } else {
        struct cover *c = &p->covers[index];
        c->state = FAILED;
        c->retry_delay = transient ? SDL_min(c->retry_delay ? 2 * c->retry_delay : 5000u, 120000u) : 0;
        c->retry_at = SDL_GetTicks() + c->retry_delay;
        network_diag("stage=steam.artwork.retry appid=%u delay_ms=%u", appid, c->retry_delay);
      }
    }
    SDL_UnlockMutex(p->mutex);
    if (loaded) {
      SDL_Event event = {.type = SDL_USEREVENT};
      SDL_PushEvent(&event);
    }
    launcher_icon_free(&icon);
  }
done:
  if (curl)
    curl_easy_cleanup(curl);
  free(data);
  return 0;
}
struct steam_artwork *steam_artwork_create(const char *root) {
  struct steam_artwork *p = calloc(1, sizeof(*p));
  char steam[512];
  DIR *directory;
  struct dirent *entry;
  int n;
  if (!p)
    return NULL;
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    free(p);
    return NULL;
  }
  steam_settings_load(root, p->language);
  p->mutex = SDL_CreateMutex();
  p->condition = SDL_CreateCond();
  n = snprintf(steam, sizeof(steam), "%s/steam", root);
  if (n > 0 && (size_t)n < sizeof(steam) && dir(steam)) {
    n = snprintf(p->cache, sizeof(p->cache), "%s/artwork", steam);
    if (n < 0 || (size_t)n >= sizeof(p->cache) || !dir(p->cache))
      p->cache[0] = 0;
  }
  if (p->cache[0] && (directory = opendir(p->cache))) {
    while ((entry = readdir(directory))) {
      char path[560], *end;
      unsigned long appid;
      struct stat st;
      errno = 0;
      appid = strtoul(entry->d_name, &end, 10);
      if (errno || !appid || appid > UINT32_MAX || end == entry->d_name ||
          strcmp(end, ".jpg"))
        continue;
      n = snprintf(path, sizeof(path), "%s/%s", p->cache, entry->d_name);
      if (n > 0 && (size_t)n < sizeof(path) && regular(path) &&
          !lstat(path, &st)) {
        p->cache_bytes += st.st_size;
        ++p->cache_count;
      }
    }
    closedir(directory);
  }
  if (p->mutex && p->condition)
    p->thread =
        SDL_CreateThreadWithStackSize(run, "steam-art", 256u * 1024u, p);
  if (!p->thread) {
    network_diag("stage=steam.artwork.worker result=start_failed");
    steam_artwork_destroy(p);
    return NULL;
  }
  return p;
}
void steam_artwork_request(struct steam_artwork *p, uint32_t appid) {
  unsigned int i;
  int chosen = -1;
  unsigned int oldest = ~0u;
  if (!p || !appid)
    return;
  SDL_LockMutex(p->mutex);
  for (i = 0; i < ART_SLOTS; ++i) {
    if (p->covers[i].appid == appid) {
      struct cover *c = &p->covers[i];
      c->use = ++p->use;
      if (c->state == FAILED && c->retry_delay && SDL_TICKS_PASSED(SDL_GetTicks(), c->retry_at)) {
        c->state = NEED_DOWNLOAD;
        SDL_CondSignal(p->condition);
      }
      SDL_UnlockMutex(p->mutex);
      return;
    }
    if (p->covers[i].state != LOADING && p->covers[i].use < oldest) {
      oldest = p->covers[i].use;
      chosen = (int)i;
    }
  }
  if (chosen >= 0) {
    struct cover *c = &p->covers[chosen];
    if (c->texture)
      SDL_DestroyTexture(c->texture);
    launcher_icon_free(&c->decoded);
    *c = (struct cover){.appid = appid, .use = ++p->use, .state = CHECK_CACHE};
    SDL_CondSignal(p->condition);
  }
  SDL_UnlockMutex(p->mutex);
}
void steam_artwork_pause(struct steam_artwork *p, int paused) {
  if (p) {
    SDL_AtomicSet(&p->paused, paused);
    SDL_CondSignal(p->condition);
  }
}
void steam_artwork_tick(struct steam_artwork *p, SDL_Renderer *renderer) {
  unsigned int i;
  if (!p)
    return;
  for (i = 0; i < ART_SLOTS; ++i) {
    struct launcher_icon icon = {0};
    SDL_Surface *surface;
    SDL_LockMutex(p->mutex);
    if (p->covers[i].decoded.data) {
      icon = p->covers[i].decoded;
      memset(&p->covers[i].decoded, 0, sizeof(icon));
    }
    SDL_UnlockMutex(p->mutex);
    if (!icon.data)
      continue;
    surface = SDL_CreateRGBSurfaceWithFormatFrom(
        icon.data, icon.width, icon.height, 32, icon.width * 4,
        SDL_PIXELFORMAT_RGBA32);
    if (surface) {
      p->covers[i].texture = SDL_CreateTextureFromSurface(renderer, surface);
      SDL_FreeSurface(surface);
    }
    p->covers[i].width = icon.width;
    p->covers[i].height = icon.height;
    if (!p->covers[i].texture)
      network_diag("stage=steam.artwork.texture appid=%u result=failed", p->covers[i].appid);
    launcher_icon_free(&icon);
  }
}
SDL_Texture *steam_artwork_texture(struct steam_artwork *p, uint32_t appid,
                                   int *w, int *h) {
  unsigned int i;
  if (!p)
    return NULL;
  for (i = 0; i < ART_SLOTS; ++i)
    if (p->covers[i].appid == appid && p->covers[i].texture) {
      *w = p->covers[i].width;
      *h = p->covers[i].height;
      return p->covers[i].texture;
    }
  return NULL;
}
void steam_artwork_destroy(struct steam_artwork *p) {
  unsigned int i;
  if (!p)
    return;
  SDL_AtomicSet(&p->stop, 1);
  if (p->condition)
    SDL_CondSignal(p->condition);
  if (p->thread)
    SDL_WaitThread(p->thread, NULL);
  for (i = 0; i < ART_SLOTS; ++i) {
    if (p->covers[i].texture)
      SDL_DestroyTexture(p->covers[i].texture);
    launcher_icon_free(&p->covers[i].decoded);
  }
  if (p->condition)
    SDL_DestroyCond(p->condition);
  if (p->mutex)
    SDL_DestroyMutex(p->mutex);
  free(p);
}
