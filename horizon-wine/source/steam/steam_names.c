#define _POSIX_C_SOURCE 200809L
#include "steam_names.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Lower-case hexadecimal after '~' is the private SD spelling. Literal Steam
 * names with that spelling reserve their slot before any aliases are assigned. */
enum { BUCKETS = 1021 };
struct component {
  struct component *next_name, *next_disk;
  char disk[10];
  char name[];
};
struct steam_names {
  struct component *names[BUCKETS], *disk[BUCKETS];
};
static unsigned hash(const char *p, size_t n) {
  unsigned h = 2166136261u;
  while (n--) {
    unsigned char c = (unsigned char)*p++;
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
    h = (h ^ c) * 16777619u;
  }
  return h;
}
static int alias_n(const char *s, size_t n) {
  if (n < 5 || n > 9 || *s != '~')
    return 0;
  for (size_t i = 1; i < n; ++i)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
          (s[i] >= 'A' && s[i] <= 'F')))
      return 0;
  return 1;
}
int steam_names_alias(const char *s) { return alias_n(s, strlen(s)); }
static int nonascii(const char *s, size_t n) {
  while (n--)
    if ((unsigned char)*s++ >= 128)
      return 1;
  return 0;
}
static struct component *lookup(const struct steam_names *m, const char *p, size_t n, int fold) {
  struct component *alternative = NULL;
  if (m)
    for (struct component *e = m->names[hash(p, n) % BUCKETS]; e; e = e->next_name) {
      if (strlen(e->name) != n)
        continue;
      if (!memcmp(e->name, p, n))
        return e;
      if (fold && !strncasecmp(e->name, p, n))
        alternative = e;
    }
  return alternative;
}
static struct component *add(struct steam_names **m, const char *name, size_t n) {
  if (!*m && !(*m = calloc(1, sizeof(**m))))
    return NULL;
  struct component *e = calloc(1, sizeof(*e) + n + 1);
  if (!e)
    return NULL;
  unsigned b = hash(name, n) % BUCKETS;
  memcpy(e->name, name, n);
  e->next_name = (*m)->names[b];
  (*m)->names[b] = e;
  return e;
}
int steam_names_collect(struct steam_names **m, const char *p) {
  while (*p) {
    size_t n = strcspn(p, "/");
    if ((nonascii(p, n) || alias_n(p, n)) && !lookup(*m, p, n, 0) && !add(m, p, n))
      return 0;
    p += n;
    if (*p)
      ++p;
  }
  return 1;
}
const char *steam_names_original(const struct steam_names *m, const char *disk) {
  if (m)
    for (struct component *e = m->disk[hash(disk, strlen(disk)) % BUCKETS]; e; e = e->next_disk)
      if (!strcasecmp(disk, e->disk))
        return e->name;
  return NULL;
}
static void index_disk(struct steam_names *m, struct component *e) {
  unsigned b = hash(e->disk, strlen(e->disk)) % BUCKETS;
  e->next_disk = m->disk[b];
  m->disk[b] = e;
}
size_t steam_names_finish(struct steam_names *m) {
  size_t mapped = 0;
  if (!m)
    return 0;
  for (size_t b = 0; b < BUCKETS; ++b)
    for (struct component *e = m->names[b]; e; e = e->next_name) {
      if (!nonascii(e->name, strlen(e->name)))
        continue;
      size_t len = strlen(e->name);
      unsigned id = hash(e->name, len);
      if (len < 9) id &= 0xffff; /* Keep deeply nested, short components compact. */
      do {
        snprintf(e->disk, sizeof(e->disk), "~%04x", id++);
      } while (lookup(m, e->disk, strlen(e->disk), 1) || steam_names_original(m, e->disk));
      index_disk(m, e);
      ++mapped;
    }
  return mapped;
}
int steam_names_path(const struct steam_names *m, const char *p, char *out, size_t cap) {
  while (*p) {
    size_t n = strcspn(p, "/"), used = n;
    const char *s = p;
    struct component *e = nonascii(p, n) ? lookup(m, p, n, 1) : NULL;
    if (e && *e->disk) {
      s = e->disk;
      used = strlen(s);
    }
    if (used >= cap) {
      errno = ENAMETOOLONG;
      return 0;
    }
    memcpy(out, s, used);
    out += used;
    cap -= used;
    p += n;
    if (*p) {
      if (cap < 2) {
        errno = ENAMETOOLONG;
        return 0;
      }
      *out++ = *p++;
      --cap;
    }
  }
  if (!cap) {
    errno = ENAMETOOLONG;
    return 0;
  }
  *out = 0;
  return 1;
}
void steam_names_free(struct steam_names *m) {
  if (!m)
    return;
  for (size_t b = 0; b < BUCKETS; ++b) {
    struct component *e = m->names[b];
    while (e) {
      struct component *next = e->next_name;
      free(e);
      e = next;
    }
  }
  free(m);
}
int steam_names_write(const struct steam_names *m, FILE *file) {
  if (m)
    for (size_t b = 0; b < BUCKETS; ++b)
      for (struct component *e = m->names[b]; e; e = e->next_name)
        if (*e->disk && fprintf(file, "sdname1=%s/%s\n", e->disk, e->name) < 0)
          return 0;
  return 1;
}
int steam_names_read(struct steam_names **m, FILE *file) {
  char line[512];
  while (fgets(line, sizeof(line), file)) {
    if (strncmp(line, "sdname1=", 8))
      continue;
    char *disk = line + 8, *name = strchr(disk, '/');
    if (!name)
      goto invalid;
    *name++ = 0;
    size_t n = strcspn(name, "\n");
    if (name[n] != '\n' || n > 255 || !steam_names_alias(disk) || !nonascii(name, n) ||
        strpbrk(name, "/\\:\"<>|?*") || lookup(*m, name, n, 0) || steam_names_original(*m, disk))
      goto invalid;
    for (size_t i = 0; i < n; ++i)
      if ((unsigned char)name[i] < 32)
        goto invalid;
    struct component *e = add(m, name, n);
    if (!e)
      return 0;
    strcpy(e->disk, disk);
    index_disk(*m, e);
  }
  return !ferror(file);
invalid:
  errno = EINVAL;
  return 0;
}
