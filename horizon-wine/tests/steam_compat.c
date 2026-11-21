#include "../source/steam/steam_compat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *store = "{\"42\":{\"success\":true,\"data\":{\"mac_requirements\":{\"minimum\":\"Memory: 64 GB\"},"
        "\"pc_requirements\":{\"recommended\":\"Memory: 8 GB\",\"minimum\":\"<li><strong>Memory:</strong> 2 GB RAM</li>"
        "<li><strong>Graphics:</strong> 256 MB video memory</li><br \\/><strong>DirectX:</strong> Version 10\"}}}}";
    struct steam_minimum m;
    assert(steam_compat_parse_minimum(store, strlen(store), 42, &m));
    assert(!strcmp(m.memory, "2 GB RAM") && !strcmp(m.directx, "Version 10"));
    assert(steam_compat_estimate(&m).fit == FIT_UNKNOWN); /* No CPU listed. */
    strcpy(m.processor, "Intel Pentium 4 1.8GHz dual core");
    assert(steam_compat_estimate(&m).fit == FIT_PROMISING);
    strcpy(m.memory, "8192 MB RAM");
    assert(steam_compat_estimate(&m).fit == FIT_HIGH_RISK);
    strcpy(m.memory, "2GB"); strcpy(m.directx, "Version 12");
    assert(strstr(steam_compat_estimate(&m).reason, "DirectX 12"));
    strcpy(m.directx, "Version 11"); strcpy(m.graphics, "GTX 1060 (3 GB)");
    assert(strstr(steam_compat_estimate(&m).reason, "shared memory"));
    strcpy(m.directx, "Version 9.0"); strcpy(m.graphics, "NVIDIA Geforce GT440（512M）or AMD equivalent");
    strcpy(m.processor, "Intel Core2 Duo E6550 or AMD equivalent or above");
    assert(steam_compat_estimate(&m).fit == FIT_PROMISING);
    strcpy(m.memory, "2 MB RAM");
    assert(steam_compat_estimate(&m).fit == FIT_PROMISING);
    assert(strstr(steam_compat_estimate(&m).reason, "unusually low"));
    strcpy(m.memory, "4 GiB"); assert(steam_compat_estimate(&m).fit == FIT_HIGH_RISK);
    strcpy(m.memory, "4,096 MB RAM"); assert(steam_compat_estimate(&m).fit == FIT_HIGH_RISK);
    strcpy(m.memory, "3 GB"); assert(steam_compat_estimate(&m).fit == FIT_TIGHT);
    strcpy(m.graphics, "1 GB VRAM"); assert(steam_compat_estimate(&m).fit == FIT_HIGH_RISK);
    strcpy(m.memory, "1 GB"); strcpy(m.graphics, "GeForce 960M");
    assert(steam_compat_estimate(&m).fit == FIT_UNKNOWN);
    strcpy(m.processor, "AMD Ryzen 5 3600"); assert(steam_compat_estimate(&m).fit == FIT_HIGH_RISK);
    strcpy(m.processor, "Intel i5-2500"); assert(steam_compat_estimate(&m).fit == FIT_TIGHT);
    strcpy(m.processor, "2.0GHz"); assert(steam_compat_estimate(&m).fit == FIT_UNKNOWN);
    struct steam_minimum low = {.memory = "512 MB RAM", .directx = "9.0c"};
    assert(steam_compat_estimate(&low).fit == FIT_UNKNOWN);
    strcpy(low.processor, "Pentium 4");
    assert(steam_compat_estimate(&low).fit == FIT_PROMISING);
    low.processor[0] = 0;
    strcpy(low.graphics, "256 MB video RAM");
    assert(steam_compat_estimate(&low).fit == FIT_PROMISING);
    strcpy(low.directx, "Version 11");
    assert(steam_compat_estimate(&low).fit == FIT_UNKNOWN);
    assert(strstr(steam_compat_estimate(&low).reason, "translation path"));
    strcpy(low.directx, "Version 12");
    assert(steam_compat_estimate(&low).fit == FIT_HIGH_RISK);
    assert(!steam_compat_parse_minimum(store, strlen(store), 43, &m));
    assert(!steam_compat_parse_minimum(store, 30, 42, &m));
    const char *legacy = "{\"42\":{\"success\":true,\"data\":{\"pc_requirements\":{\"minimum\":\"Video Card: DirectX® 9.0c with 256 MB\"}}}}";
    assert(steam_compat_parse_minimum(legacy, strlen(legacy), 42, &m));
    assert(!strcmp(m.directx, "9.0c") && strstr(m.graphics, "256 MB"));
    const char *empty = "{\"42\":{\"success\":true,\"data\":{\"pc_requirements\":[]}}}";
    assert(steam_compat_parse_minimum(empty, strlen(empty), 42, &m));
    assert(steam_compat_estimate(&m).fit == FIT_UNKNOWN);
    assert(!steam_compat_estimate(NULL).reason);
    puts("Steam Windows minimum, Switch records and estimate checks passed.");
    return 0;
}
