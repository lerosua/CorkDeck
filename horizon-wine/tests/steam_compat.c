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
    assert(strstr(steam_compat_estimate(&m), "Worth testing"));
    strcpy(m.memory, "8192 MB RAM");
    assert(strstr(steam_compat_estimate(&m), "Windows RAM minimum"));
    strcpy(m.memory, "2GB"); strcpy(m.directx, "Version 12");
    assert(strstr(steam_compat_estimate(&m), "DirectX 12"));
    strcpy(m.directx, "Version 11"); strcpy(m.graphics, "GTX 1060 (3 GB)");
    assert(strstr(steam_compat_estimate(&m), "shared Switch memory"));
    assert(!steam_compat_parse_minimum(store, strlen(store), 43, &m));
    assert(!steam_compat_parse_minimum(store, 30, 42, &m));
    const char *legacy = "{\"42\":{\"success\":true,\"data\":{\"pc_requirements\":{\"minimum\":\"Video Card: DirectX® 9.0c with 256 MB\"}}}}";
    assert(steam_compat_parse_minimum(legacy, strlen(legacy), 42, &m));
    assert(!strcmp(m.directx, "9.0c") && strstr(m.graphics, "256 MB"));
    const char *empty = "{\"42\":{\"success\":true,\"data\":{\"pc_requirements\":[]}}}";
    assert(steam_compat_parse_minimum(empty, strlen(empty), 42, &m));
    assert(strstr(steam_compat_estimate(&m), "Insufficient"));
    const char *deck = "{\"success\":1,\"results\":{\"machine_resolved_category\":3,\"appid\":42,\"resolved_category\":1}}";
    unsigned int rating;
    assert(steam_compat_parse_deck(deck, strlen(deck), 42, &rating) && rating == 1);
    assert(!steam_compat_parse_deck(deck, strlen(deck), 43, &rating));
    assert(!steam_compat_parse_deck(empty, strlen(empty), 42, &rating));
    assert(strstr(steam_compat_record(205100), "main menu"));
    assert(strstr(steam_compat_record(17410), "blocked"));
    assert(!steam_compat_record(42));
    puts("Steam Windows minimum, Deck-specific rating, Switch records and estimate checks passed.");
    return 0;
}
