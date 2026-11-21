#include "steam_compat.h"
#include "../json_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

/* Title-level observations in docs/COMPATIBILITY.md. Keep editions separate. */
const char *steam_compat_record(uint32_t app)
{
    switch (app) {
    case 7940: return "0.2 reached the opening logos. Gameplay untested.";
    case 22300: return "0.2 reached the launcher and main menu. Gameplay untested.";
    case 329860: return "An earlier build reached the main menu. Gameplay untested.";
    case 205100: return "0.2 reached the main menu at about 24 FPS. No gameplay measurement in that test.";
    case 15100: return "0.3e reached the intro video. Menu and gameplay untested.";
    case 17410: return "Startup blocked by the Steam client signature check.";
    case 42700: return "Exits before the first frame. This edition needs Steam client executable generation.";
    case 223850: return "Fails to start. Requires the Steam client and its system information service.";
    default: return NULL;
    }
}

/* Return just the value at a given level; the store also sends recommended
 * requirements and separate SteamOS/Machine ratings with similar names. */
static int member(struct parser object, const char *name, struct parser *value)
{
    if (!consume(&object, '{')) return 0;
    do {
        char key[128];
        if (!json_string(&object, key, sizeof(key)) || !consume(&object, ':')) return 0;
        struct parser start = object;
        if (!skip_value(&object)) return 0;
        if (!strcmp(name, key)) {
            start.end = object.p;
            *value = start;
            return 1;
        }
    } while (consume(&object, ','));
    return 0;
}

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
    if (!member(root, id, &entry) || !member(entry, "success", &field) ||
        !json_bool(&field, &success) || !success || !member(entry, "data", &data)) return 0;
    if (!member(data, "pc_requirements", &requirements) || !member(requirements, "minimum", &field)) return 1;
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

int steam_compat_parse_deck(const char *json, size_t size, uint32_t app, unsigned int *category)
{
    struct parser root = {(const unsigned char *)json, (const unsigned char *)json + size, 0};
    struct parser result, field;
    unsigned long long value;
    if (!member(root, "success", &field) || !json_uint(&field, &value) || value != 1 ||
        !member(root, "results", &result) || !member(result, "appid", &field) ||
        !json_uint(&field, &value) || value != app || !member(result, "resolved_category", &field) ||
        !json_uint(&field, &value) || value > 3) return 0;
    *category = (unsigned int)value;
    return 1;
}

static double megabytes(const char *text)
{
    for (const char *p = text; *p;) {
        if (!isdigit((unsigned char)*p)) { ++p; continue; }
        char *end;
        double number = strtod(p, &end);
        p = end;
        while (isspace((unsigned char)*p)) ++p;
        if (!strncasecmp(p, "GB", 2) || !strncasecmp(p, "GiB", 3)) return number * 1024;
        if (!strncasecmp(p, "MB", 2) || !strncasecmp(p, "MiB", 3)) return number;
    }
    return 0;
}

const char *steam_compat_estimate(const struct steam_minimum *minimum)
{
    double ram = megabytes(minimum->memory), vram = megabytes(minimum->graphics);
    const char *version = minimum->directx + strcspn(minimum->directx, "0123456789");
    long dx = strtol(version, NULL, 10);
    if (ram >= 4096) return "High risk: the Windows RAM minimum leaves little room for Horizon and translation.";
    if (dx >= 12) return "High risk: the Windows build requires DirectX 12 graphics translation.";
    if (vram >= 2048) return "High risk: the listed graphics memory competes for shared Switch memory.";
    if (ram > 0 && ram <= 2048 && dx >= 8 && dx <= 10 && vram > 0 && vram <= 512)
        return "Worth testing: low RAM and graphics memory requirements, with an older DirectX version.";
    if (ram > 0 && ram <= 2048 && (dx == 0 || dx <= 11) &&
        strstr(minimum->graphics, "Intel") &&
        (strstr(minimum->graphics, "HD 3000") || strstr(minimum->graphics, "HD 4000") ||
         strstr(minimum->graphics, "HD Graphics 3000") || strstr(minimum->graphics, "HD Graphics 4000")))
        return "Worth testing: low RAM requirements and an older integrated GPU are listed.";
    return "Insufficient evidence to predict gameplay from these Windows requirements.";
}
