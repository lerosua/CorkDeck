/* Exercise the actual Steam registration boundary and unchanged first-add
 * renderer rule using the existing offline launcher fixtures. */
#define main launcher_fixture_main
#include "launcher_host.c"
#undef main
#include "../source/launcher.c"

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
