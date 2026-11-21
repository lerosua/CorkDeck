#include "steam_compat.h"
#include "../json_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

static void plain_text(char *html)
{
    char *out = html;
    for (const char *p = html; *p;) {
        if (*p == '<') {
            const char *end = strchr(p, '>');
            if (!end) break;
            if (!strncasecmp(p, "<br", 3) || !strncasecmp(p, "</li", 4) ||
                !strncasecmp(p, "</p", 3)) *out++ = '\n';
            p = end + 1;
        } else if (!strncasecmp(p, "&nbsp;", 6)) {
            *out++ = ' '; p += 6;
        } else if (!strncasecmp(p, "&amp;", 5)) {
            *out++ = '&'; p += 5;
        } else {
            *out++ = *p++;
        }
    }
    *out = 0;
}

static void copy_field(char *out, size_t capacity, const char *value)
{
    while (isspace((unsigned char)*value)) ++value;
    size_t length = strlen(value);
    while (length && isspace((unsigned char)value[length-1])) --length;
    if (length >= capacity) {
        length = capacity-1;
        while (length && ((unsigned char)value[length] & 0xc0) == 0x80) --length;
    }
    memcpy(out, value, length);
    out[length] = 0;
}

int steam_compat_parse_minimum(const char *json, size_t size, uint32_t app, struct steam_minimum *minimum)
{
    struct parser root = {(const unsigned char *)json, (const unsigned char *)json + size, 0};
    struct parser entry, field, data, requirements;
    char id[16], html[16384];
    int success;
    *minimum = (struct steam_minimum){0};
    snprintf(id, sizeof(id), "%u", app);
    if (!json_member(root, id, &entry) || !json_member(entry, "success", &field) ||
        !json_bool(&field, &success) || !success || !json_member(entry, "data", &data)) return 0;
    if (!json_member(data, "pc_requirements", &requirements) || !json_member(requirements, "minimum", &field)) return 1;
    if (!json_string(&field, html, sizeof(html))) return 0;
    plain_text(html);
    for (char *line = html; line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) *next++ = 0;
        while (isspace((unsigned char)*line)) ++line;
        char *value = strchr(line, ':');
        if (value) {
            *value++ = 0;
            if (!strcasecmp(line, "Memory") || !strcasecmp(line, "RAM")) copy_field(minimum->memory, sizeof(minimum->memory), value);
            if (!strcasecmp(line, "DirectX") || !strcasecmp(line, "DirectX®") || !strcasecmp(line, "DirectX Version")) copy_field(minimum->directx, sizeof(minimum->directx), value);
            if (!strcasecmp(line, "Graphics") || !strcasecmp(line, "Video Card") ||
                !strcasecmp(line, "Video") || !strcasecmp(line, "Graphics Card")) copy_field(minimum->graphics, sizeof(minimum->graphics), value);
            if (!strcasecmp(line, "Processor") || !strcasecmp(line, "CPU")) copy_field(minimum->processor, sizeof(minimum->processor), value);
        }
        line = next;
    }
    /* Older store entries put the DirectX version inside the video card field. */
    if (!minimum->directx[0]) {
        for (const char *p = minimum->graphics; *p; ++p) {
            if (!strncasecmp(p, "DirectX", 7)) p += 7;
            else if (!strncasecmp(p, "Direct X", 8)) p += 8;
            else continue;
            if (!strncmp(p, "®", strlen("®"))) p += strlen("®");
            while (isspace((unsigned char)*p)) ++p;
            if (isdigit((unsigned char)*p)) {
                size_t n = strspn(p, "0123456789.abc");
                if (n < sizeof(minimum->directx)) {
                    memcpy(minimum->directx, p, n);
                    minimum->directx[n] = 0;
                }
            }
            break;
        }
    }
    return 1;
}

static double megabytes(const char *text)
{
    for (const char *p = text; *p;) {
        if (!isdigit((unsigned char)*p)) { ++p; continue; }
        const char *start = p;
        char *end;
        double number = strtod(p, &end);
        p = end;
        /* English store entries can group thousands, e.g. "4,096 MB". */
        while (*p == ',' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) &&
               isdigit((unsigned char)p[3]) && !isdigit((unsigned char)p[4])) {
            number = number * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
            p += 4;
        }
        while (isspace((unsigned char)*p)) ++p;
        if (!strncasecmp(p, "GB", 2) || !strncasecmp(p, "GiB", 3)) return number * 1024;
        if (!strncasecmp(p, "MB", 2) || !strncasecmp(p, "MiB", 3)) return number;
        /* Some publishers write VRAM as "(512M)", including full-width brackets.
         * A GPU model such as "960M" alone is not a memory capacity. */
        if ((*p == 'M' || *p == 'm') && (p[1] == ')' || !strncmp(p + 1, "）", 3))) {
            if ((start > text && start[-1] == '(') ||
                (start - text >= 3 && !memcmp(start - 3, "（", 3))) return number;
        }
    }
    return 0;
}

/* Match named CPU families and explicit core requirements. GHz from an x86
 * spec cannot be converted into Cortex-A57 performance under translation. */
static int contains_any(const char *text, const char *const terms[], size_t count)
{
    for (size_t i = 0; i < count; ++i)
        for (const char *p = text; *p; ++p)
            if (!strncasecmp(p, terms[i], strlen(terms[i]))) return 1;
    return 0;
}
#define MATCH(text, terms) contains_any(text, terms, sizeof(terms) / sizeof((terms)[0]))

struct steam_assessment steam_compat_estimate(const struct steam_minimum *m)
{
    if (!m) return (struct steam_assessment){FIT_UNKNOWN, NULL};
    static const char *const old_cpu[] = {"core 2 duo", "core2 duo", "pentium 4", "pentium iii",
                                         "pentium d ", "athlon 64", "athlon ii x2"};
    static const char *const heavy_cpu[] = {"6-core", "6 core", "six-core", "six core",
                                          "8-core", "8 core", "eight-core", "eight core",
                                          "ryzen 5", "ryzen 7", "ryzen 9", "core i9"};
    static const char *const desktop_cpu[] = {"core i3", "core i5", "core i7", "i3-", "i5-", "i7-",
                                              "ryzen", "quad core", "quad-core", "4 cores", "4-core"};
    static const char *const old_igpu[] = {"HD 3000", "HD 4000", "HD Graphics 3000", "HD Graphics 4000"};
    double ram = megabytes(m->memory), vram = megabytes(m->graphics);
    long dx = strtol(m->directx + strcspn(m->directx, "0123456789"), NULL, 10);
    if (ram >= 4096)
        return (struct steam_assessment){FIT_HIGH_RISK, "The game's Windows RAM minimum reaches or exceeds the Switch's 4 GB of shared memory."};
    if (dx >= 12)
        return (struct steam_assessment){FIT_HIGH_RISK, "The Windows build requires DirectX 12 graphics translation."};
    if (vram >= 2048 || (vram > 0 && ram + vram >= 4096))
        return (struct steam_assessment){FIT_HIGH_RISK, "The GPU and game compete for the Switch's 4 GB of shared memory."};
    int legacy = MATCH(m->processor, old_cpu);
    if (!legacy && MATCH(m->processor, heavy_cpu))
        return (struct steam_assessment){FIT_HIGH_RISK, "The listed desktop CPU is demanding for four Cortex-A57 cores translating x86 code."};
    if (ram > 2048)
        return (struct steam_assessment){FIT_TIGHT, "The RAM minimum leaves little room for graphics, Horizon and the translator."};
    if (!legacy && MATCH(m->processor, desktop_cpu))
        return (struct steam_assessment){FIT_TIGHT, "Translating this desktop CPU workload on Cortex-A57 may limit frame rate."};
    int light_gpu = (vram > 0 && vram <= 512 && dx >= 8 && dx <= 10) ||
                    (dx <= 11 && strstr(m->graphics, "Intel") && MATCH(m->graphics, old_igpu));
    if (legacy && ram >= 64 && ram <= 2048 && light_gpu)
        return (struct steam_assessment){FIT_PROMISING, "An older x86 CPU, modest RAM and light graphics make this worth trying."};
    if (ram > 0 && ram < 64) {
        if (legacy && light_gpu)
            return (struct steam_assessment){FIT_PROMISING, "The store RAM value looks unusually low. The older CPU and modest graphics requirements make this worth trying."};
        return (struct steam_assessment){FIT_UNKNOWN, "The store lists an unusually small RAM minimum. A reliable estimate needs better data."};
    }
    if (ram >= 64 && ram <= 1024 && dx >= 9 && dx <= 11) {
        if (dx <= 10 && ((legacy && !m->graphics[0]) || (light_gpu && !m->processor[0])))
            return (struct steam_assessment){FIT_PROMISING, "Low RAM and modest Direct3D 9/10 requirements make this worth trying. CPU or GPU details are incomplete."};
        return (struct steam_assessment){FIT_UNKNOWN, "RAM demand is low and a Direct3D translation path exists. The listed hardware gives no clear frame-rate estimate."};
    }
    return (struct steam_assessment){FIT_UNKNOWN, "The listed Windows requirements give too little evidence to predict gameplay."};
}
