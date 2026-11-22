/* Exercise the actual Steam registration boundary and unchanged first-add
 * renderer rule using the existing offline launcher fixtures. */
#define main launcher_fixture_main
#include "launcher_host.c"
#undef main
#include "../source/launcher.c"

static Uint32 wake_idle_test(Uint32 interval, void *unused)
{
    SDL_Event event = {.type = SDL_USEREVENT};
    (void)interval;
    (void)unused;
    SDL_PushEvent(&event);
    return 0;
}

static void network_indicator(struct ui *ui)
{
    const struct nx_network states[] = {
        {NX_LINK_WIFI, 0}, {NX_LINK_WIFI, 1}, {NX_LINK_WIFI, 2}, {NX_LINK_WIFI, 3},
        {NX_LINK_DOWN, 0}, {NX_LINK_ETHERNET, 0}, {NX_LINK_CONNECTING, 0}
    };
    SDL_Rect area = {0, 0, 32, 26};
    Uint32 images[SDL_arraysize(states)][32 * 26];
    int animations = ui->animations;
    ui->animations = 0;
    assert(ui_begin_frame(ui));
    for (size_t i = 0; i < SDL_arraysize(states); ++i) {
        ui_fill(ui, 0, 0, area.w, area.h, (SDL_Color){0, 0, 0, 255});
        draw_network_status(ui, states[i], 30, 13);
        assert(!SDL_RenderReadPixels(ui->renderer, &area, SDL_PIXELFORMAT_RGBA32, images[i], 32 * 4));
        for (size_t j = 0; j < i; ++j)
            assert(memcmp(images[j], images[i], sizeof(images[i])));
    }
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_TimerID watchdog = SDL_AddTimer(4000, wake_idle_test, NULL);
    assert(watchdog);
    Uint32 start = SDL_GetTicks();
    ui_wait(ui);
    assert(SDL_GetTicks() - start >= 1000 && SDL_RemoveTimer(watchdog));
    assert(ui->running && !ui->queued_count);
    ui->animations = animations;
    puts("Wi-Fi strengths, offline, Ethernet and connecting render distinctly; idle status refresh wakes without input");
}

static void steam_indicator(struct launcher *l)
{
    struct ui *ui = &l->ui;
    const SDL_Rect badge = {40, 16, 26, 26}, wheel = {20, 16, 18, 26};
    Uint8 idle[26 * 26 * 4], busy[sizeof(idle)], online[sizeof(idle)];
    Uint8 before[18 * 26 * 4], after[sizeof(before)], blank[sizeof(before)];
    struct steam_status_view status = {.state = STEAM_WAIT_NETWORK, .restoring = 1};
    l->symbols[SYMBOL_STEAM_STATUS] = ui_svg_texture(ui, icon_steam.d, 0, 0, 24, 24, 26);
    assert(l->symbols[SYMBOL_STEAM_STATUS]);
    shell_language = APP_LANG_ZH_CN;
    int animations = ui->animations;
    ui->animations = 0;
    ui_fill(ui, 16, 12, 58, 36, (SDL_Color){0, 0, 0, 255});
    assert(!SDL_RenderReadPixels(ui->renderer, &wheel, SDL_PIXELFORMAT_RGBA32, blank, wheel.w * 4));
    int left = sh_draw_steam_status(l, &status, 66, 29);
    assert(!strcmp(sh.last_steam_label, "Steam 离线，等待重连"));
    assert(!SDL_RenderReadPixels(ui->renderer, &badge, SDL_PIXELFORMAT_RGBA32, idle, badge.w * 4));
    assert(!SDL_RenderReadPixels(ui->renderer, &wheel, SDL_PIXELFORMAT_RGBA32, before, wheel.w * 4));
    assert(!memcmp(blank, before, sizeof(before)));

    status.state = STEAM_CONNECTING;
    ui_fill(ui, 16, 12, 58, 36, (SDL_Color){0, 0, 0, 255});
    assert(sh_draw_steam_status(l, &status, 66, 29) == left);
    assert(!strcmp(sh.last_steam_label, "正在恢复 Steam 会话") && ui->status_refresh_ms == 100);
    assert(!SDL_RenderReadPixels(ui->renderer, &badge, SDL_PIXELFORMAT_RGBA32, busy, badge.w * 4));
    assert(!memcmp(idle, busy, sizeof(idle)));
    assert(!SDL_RenderReadPixels(ui->renderer, &wheel, SDL_PIXELFORMAT_RGBA32, before, wheel.w * 4));
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    ui_wait(ui);
    ui_fill(ui, 16, 12, 58, 36, (SDL_Color){0, 0, 0, 255});
    sh_draw_steam_status(l, &status, 66, 29);
    assert(!SDL_RenderReadPixels(ui->renderer, &wheel, SDL_PIXELFORMAT_RGBA32, after, wheel.w * 4));
    assert(memcmp(before, after, sizeof(before)));

    status.state = STEAM_ONLINE;
    ui_fill(ui, 16, 12, 58, 36, (SDL_Color){0, 0, 0, 255});
    assert(sh_draw_steam_status(l, &status, 66, 29) == left);
    assert(!strcmp(sh.last_steam_label, "Steam 已登录"));
    assert(!SDL_RenderReadPixels(ui->renderer, &badge, SDL_PIXELFORMAT_RGBA32, online, badge.w * 4));
    assert(!SDL_RenderReadPixels(ui->renderer, &wheel, SDL_PIXELFORMAT_RGBA32, after, wheel.w * 4));
    assert(!memcmp(blank, after, sizeof(blank)));
    unsigned green = 0;
    for (size_t i = 0; i < sizeof(online); i += 4)
        if (online[i] == SH_GREEN.r && online[i+1] == SH_GREEN.g && online[i+2] == SH_GREEN.b) {
            assert(idle[i] == 153 && idle[i+1] == 159 && idle[i+2] == 166);
            ++green;
        }
    assert(green > 100);
    SDL_DestroyTexture(l->symbols[SYMBOL_STEAM_STATUS]);
    l->symbols[SYMBOL_STEAM_STATUS] = NULL;
    ui->animations = animations;
    shell_language = APP_LANG_EN;
    puts("Steam mark stays gray during recovery, spinner advances without input, online mark turns green");
}

/* Exercise the same card branch used by the 7x2 Not installed grid. Keep the
 * network paused so that a successful render must have come from the card. */
static void cached_library_grid(struct launcher *l)
{
    extern const unsigned char corkdeck_font[];
    extern const size_t corkdeck_font_size;
    SDL_Rect card = {0, 0, 150, 225}, center = {75, 112, 1, 1};
    const int count = 14;
    char path[512];
    png_image png = {.version=PNG_IMAGE_VERSION, .width=1, .height=1, .format=PNG_FORMAT_RGBA};

    assert(ui_init(&l->ui, corkdeck_font, corkdeck_font_size, 0));
    network_indicator(&l->ui);
    steam_indicator(l);
    assert(sh_library_art(l));
    steam_artwork_pause(sh.library_art, 1);
    for (int i = 0; i < count; i++) {
        uint8_t color[] = {220, 30, (uint8_t)i, 255};
        sh.owned[i].app_id = 1000 + i;
        snprintf(sh.owned[i].name, sizeof(sh.owned[i].name), "Fixture %d", i);
        snprintf(path, sizeof(path), i ? "%s/steam/artwork/%u.jpg" : "%s/steam/artwork/corkdeck-%u-cover.jpg",
                 l->options->runtime_dir, sh.owned[i].app_id);
        assert(png_image_write_to_file(&png, path, 0, color, 0, NULL));
        sh_card(l, -1-i, card, 0, 0);
    }
    for (int frame = 0; frame < 200; frame++) {
        int w, h;
        steam_artwork_tick(sh.library_art, l->ui.renderer);
        int i = 0;
        while (i < count && steam_artwork_texture(sh.library_art, sh.owned[i].app_id, &w, &h)) i++;
        if (i == count) break;
        SDL_Delay(5);
    }
    for (int i = 0; i < count; i++) {
        uint8_t pixel[4];
        sh_card(l, -1-i, card, 0, 0);
        assert(!SDL_RenderReadPixels(l->ui.renderer, &center, SDL_PIXELFORMAT_RGBA32, pixel, sizeof(pixel)));
        assert(pixel[0] == 220 && pixel[1] == 30 && pixel[2] == i);
    }
    /* An uncached game stays selectable and shows its placeholder offline. */
    sh.owned[count].app_id = 9999;
    strcpy(sh.owned[count].name, "Uncached");
    sh_card(l, -1-count, card, 0, 0);
    int w, h;
    uint8_t pixel[4];
    assert(!steam_artwork_texture(sh.library_art, 9999, &w, &h));
    assert(!SDL_RenderReadPixels(l->ui.renderer, &center, SDL_PIXELFORMAT_RGBA32, pixel, sizeof(pixel)));
    assert(pixel[0] != 220);
    /* A cold installed card has only its install marker and the cache left
     * by the uninstalled grid. It must render that image with no network. */
    const char *marker = "sdmc:/switch/wine/drive_c/Afterlight/autorun-steam-install.ini";
    FILE *file = fopen(marker, "w");
    assert(file && fputs("autorun_steam_install=1\nappid=2004770\n", file) >= 0 && !fclose(file));
    uint8_t installed[] = {27, 165, 93, 255};
    snprintf(path, sizeof(path), "%s/steam/artwork/2004770.jpg", l->options->runtime_dir);
    assert(png_image_write_to_file(&png, path, 0, installed, 0, NULL));
    sh_forget(0);
    for (int frame = 0; frame < 200; frame++) {
        sh_card(l, 0, card, 0, 0);
        assert(!SDL_RenderReadPixels(l->ui.renderer, &center, SDL_PIXELFORMAT_RGBA32, pixel, sizeof(pixel)));
        if (!memcmp(pixel, installed, 4)) break;
        SDL_Delay(5);
        steam_artwork_tick(sh.library_art, l->ui.renderer);
    }
    assert(!memcmp(pixel, installed, 4));
    assert(!remove(marker));
    puts("Installed card renders the existing AppID cover from SD offline");
    sh_art_stop();
    ui_quit(&l->ui);
    puts("All 14 uninstalled grid cards render the matching SD cover offline");
}

int main(void)
{
    struct wine_nx_launcher_options options = {.runtime_dir="sdmc:/switch/wine",.machine_of=machine_of};
    struct launcher *l = calloc(1,sizeof(*l));
    struct program manual;
    struct launcher_kv kv;
    const char *exe="sdmc:/switch/wine/drive_c/Afterlight/Game.exe";
    const char *settings="sdmc:/switch/wine/drive_c/Afterlight/Game.wine-nx.txt";
    char value[128];
    assert(l);l->options=&options;carousel_fixture();launcher_catalog_init(&l->catalog);
    assert(!remove(settings));
    assert(describe_program(l,&manual,exe));
    apply_new_game_renderer(l,&manual);
    assert(manual.settings.d3d==LAUNCHER_D3D_WINE);
    assert(!remove(settings));
    assert(steam_register_executable(l,exe,"Steam display title"));
    assert(l->program_count==1&&l->programs[0].settings.d3d==manual.settings.d3d);
    assert(!strcmp(l->programs[0].title,"Steam display title"));
    assert(launcher_kv_load(&kv,settings));
    assert(launcher_kv_set(&kv,"title",NULL));
    assert(launcher_kv_set(&kv,"d3d","dxvk"));
    assert(launcher_kv_set(&kv,"d3d9","wine"));
    assert(launcher_kv_set(&kv,"future-setting","keep"));
    assert(launcher_kv_save(&kv,settings));load_program_settings(l,&l->programs[0]);
    assert(steam_register_executable(l,exe,"Steam updated title"));
    assert(launcher_kv_load(&kv,settings));
    assert(launcher_kv_get(&kv,"d3d",value,sizeof(value))&&!strcmp(value,"dxvk"));
    assert(launcher_kv_get(&kv,"d3d9",value,sizeof(value))&&!strcmp(value,"wine"));
    assert(launcher_kv_get(&kv,"future-setting",value,sizeof(value))&&!strcmp(value,"keep"));
    assert(steam_register_executable(l,exe,"Should not replace custom title"));
    assert(!strcmp(l->programs[0].title,"Steam updated title"));
    int catalog_count=l->program_count;
    assert(steam_launch_prerequisite(l,exe));
    assert(!strcmp(l->prerequisite_target,exe)&&l->program_count==catalog_count);
    assert(!steam_launch_prerequisite(l,exe));
    l->prerequisite_target[0]=0;
    assert(!steam_launch_prerequisite(l,"sdmc:/switch/wine/missing.exe"));
    assert(launcher_kv_load(&kv,settings)&&launcher_kv_get(&kv,"d3d",value,sizeof(value))&&!strcmp(value,"dxvk"));
    cached_library_grid(l);
    free(l);puts("Steam registration keeps manual first-add renderer behavior and explicit settings");
    return 0;
}
