#define _POSIX_C_SOURCE 200809L
#include "steam_art_zip.h"
#include "steam_file.h"
#include "steam_hash.h"
#include "../network_diag.h"
#include <errno.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Horizon cannot create every UTF-8 component in the art depot for AppID 2004770.
 * Only the ZIP entries need those names. Partial downloads are kept in the
 * AppID's manifest cache until the verified installation is published. */
struct steam_art_entry {
  char name[386];
  uint64_t size;
  unsigned char sha[20];
  int directory;
};
enum { COPY_SIZE = 65536, ZIP_UTF8 = 1 << 11 };
int steam_art_member(uint32_t app, uint32_t depot, const struct steam_manifest_file *f) {
  return app == 2004770 && depot == 2004776 &&
         (!strncmp(f->path, "art-picture/", 12) || ((f->flags & 64) && !strcmp(f->path, "art-picture")));
}
int steam_art_part(const char *cache, const char *name, char out[1024]) {
  unsigned char digest[20];
  char hex[41];
  steam_sha1(name, strlen(name), digest);
  for (size_t i = 0; i < sizeof(digest); ++i) snprintf(hex + 2*i, 3, "%02x", digest[i]);
  return snprintf(out, 1024, "%s/art-%s.part", cache, hex) < 1024;
}
int steam_art_add(struct steam_art_zip *art, const struct steam_manifest_file *file) {
  /* The downloader applies repeated file mappings in manifest order. Only
   * the last version of a name belongs in the published ZIP. */
  for (size_t i = 0; i < art->count; ++i) {
    struct steam_art_entry *previous = &art->entries[i];
    if (!previous->directory && !(file->flags & 64) && !strcmp(previous->name, file->path)) {
      previous->size = file->size;
      memcpy(previous->sha, file->sha, sizeof(previous->sha));
      return 1;
    }
  }
  if (art->count == art->capacity) {
    size_t next = art->capacity ? art->capacity * 2 : 32;
    if (next < art->capacity || next > SIZE_MAX / sizeof(*art->entries)) return 0;
    void *p = realloc(art->entries, next * sizeof(*art->entries));
    if (!p) return 0;
    art->entries = p;
    art->capacity = next;
  }
  struct steam_art_entry *e = &art->entries[art->count++];
  e->directory = (file->flags & 64) != 0;
  snprintf(e->name, sizeof(e->name), "%s%s", file->path, e->directory ? "/" : "");
  e->size = file->size;
  memcpy(e->sha, file->sha, sizeof(e->sha));
  return 1;
}
static int write_member(zipFile zip, const struct steam_art_entry *e, const char *cache,
                        int (*tick)(void *), void *ctx, unsigned char *buf) {
  zip_fileinfo info = {0};
  info.external_fa = e->directory ? 0x10 : 0;
  info.tmz_date.tm_year = 1980;
  info.tmz_date.tm_mday = 1;
  int rc = zipOpenNewFileInZip4_64(zip, e->name, &info, NULL, 0, NULL, 0, NULL, 0, 0,
                                   0, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
                                   NULL, 0, 0, ZIP_UTF8, e->size >= UINT32_MAX);
  int error = rc == ZIP_OK ? 0 : EIO;
  FILE *in = NULL;
  uint64_t copied = 0;
  if (!error && !e->directory) {
    char part[1024];
    if (!steam_art_part(cache, e->name, part)) error = ENAMETOOLONG;
    else if (!(in = fopen(part, "rb"))) error = errno;
  }
  while (in && !error && copied < e->size) {
    if (!tick(ctx)) { error = ECANCELED; break; }
    size_t want = e->size - copied < COPY_SIZE ? (size_t)(e->size - copied) : COPY_SIZE;
    size_t n = fread(buf, 1, want, in);
    if (n != want) { error = ferror(in) ? EIO : EBADMSG; break; }
    if (zipWriteInFileInZip(zip, buf, (unsigned)n) != ZIP_OK) {
      error = errno == ENOSPC ? ENOSPC : EIO;
      break;
    }
    copied += n;
  }
  if (in) {
    if (!error && (fgetc(in) != EOF || ferror(in))) error = EBADMSG;
    if (fclose(in) && !error) error = errno;
  }
  if (rc == ZIP_OK && zipCloseFileInZip(zip) != ZIP_OK && !error) error = EIO;
  network_diag("stage=steam.art.zip op=%s bytes=%llu errno=%d path=%s",
               e->directory ? "directory" : "write", (unsigned long long)copied, error, e->name);
  return error;
}
static int verify_member(unzFile zip, const struct steam_art_entry *e, int (*tick)(void *),
                         void *ctx, unsigned char *buf) {
  unz_file_info64 info;
  char name[sizeof(e->name)] = {0};
  if (unzGetCurrentFileInfo64(zip, &info, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK ||
      info.size_filename != strlen(e->name) || memcmp(name, e->name, strlen(e->name)) ||
      !(info.flag & ZIP_UTF8) || info.uncompressed_size != e->size ||
      unzOpenCurrentFile(zip) != UNZ_OK) {
    network_diag("stage=steam.art.zip op=readback_header errno=%d path=%s", EBADMSG, e->name);
    return EBADMSG;
  }
  struct steam_sha1 hash;
  unsigned char digest[20];
  steam_sha1_begin(&hash);
  uint64_t bytes = 0;
  int error = 0;
  for (;;) {
    if (!tick(ctx)) { error = ECANCELED; break; }
    int n = unzReadCurrentFile(zip, buf, COPY_SIZE);
    if (n < 0) { error = EBADMSG; break; }
    if (!n) break;
    bytes += (unsigned)n;
    steam_sha1_update(&hash, buf, (size_t)n);
  }
  steam_sha1_end(&hash, digest);
  if (unzCloseCurrentFile(zip) != UNZ_OK && !error) error = EBADMSG;
  if (!error && (bytes != e->size || (bytes && memcmp(digest, e->sha, 20)))) error = EBADMSG;
  network_diag("stage=steam.art.zip op=readback bytes=%llu errno=%d path=%s",
               (unsigned long long)bytes, error, e->name);
  return error;
}
int steam_art_publish(const struct steam_art_zip *art, const char *cache, const char *stage,
                      int (*tick)(void *), void *ctx, unsigned char *buf) {
  char tmp[1024], dst[1024];
  if (snprintf(tmp, sizeof(tmp), "%s/art-picture.zip.tmp", cache) >= (int)sizeof(tmp) ||
      snprintf(dst, sizeof(dst), "%s/art-picture.zip", stage) >= (int)sizeof(dst)) return ENAMETOOLONG;
  struct stat st;
  if (!lstat(tmp, &st)) {
    if (!S_ISREG(st.st_mode)) return EINVAL;
  } else if (errno != ENOENT) return errno;
  zipFile zip = zipOpen64(tmp, APPEND_STATUS_CREATE);
  if (!zip) return errno ? errno : EIO;
  int error = 0;
  for (size_t i = 0; i < art->count && !error; ++i)
    error = write_member(zip, &art->entries[i], cache, tick, ctx, buf);
  if (zipClose(zip, NULL) != ZIP_OK && !error) error = EIO;
  if (!error) {
    FILE *f = fopen(tmp, "r+b");
    if (!f) error = errno;
    else {
      if (fsync(fileno(f))) error = errno;
      if (fclose(f) && !error) error = errno;
    }
  }
  if (!error) {
    unzFile read = unzOpen64(tmp);
    if (!read) error = EBADMSG;
    else {
      int pos = unzGoToFirstFile(read);
      for (size_t i = 0; i < art->count && !error; ++i) {
        error = pos == UNZ_OK ? verify_member(read, &art->entries[i], tick, ctx, buf) : EBADMSG;
        pos = unzGoToNextFile(read);
      }
      if (pos != UNZ_END_OF_LIST_OF_FILE && !error) error = EBADMSG;
      if (unzClose(read) != UNZ_OK && !error) error = EIO;
    }
  }
  if (!error && !steam_file_replace(tmp, dst)) error = errno ? errno : EIO;
  if (error) unlink(tmp);
  network_diag("stage=steam.art.zip op=publish entries=%zu errno=%d path=%s", art->count, error, dst);
  return error;
}
void steam_art_cleanup(const struct steam_art_zip *art, const char *cache) {
  for (size_t i = 0; i < art->count; ++i) {
    char part[1024];
    if (!art->entries[i].directory && steam_art_part(cache, art->entries[i].name, part) &&
        unlink(part) && errno != ENOENT)
      network_diag("stage=steam.art.zip op=cleanup errno=%d path=%s", errno, art->entries[i].name);
  }
}
