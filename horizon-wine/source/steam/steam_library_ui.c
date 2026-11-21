#include "steam_library_ui.h"
#include "../launcher_ui.h"
#include "../app/app_version.h"
#include "../../vendor/qrcodegen/qrcodegen.h"
#include "steam_content.h"
#include "steam_compat.h"
#include "steam_provider.h"
#include "steam_storage.h"
#include "steam_artwork.h"
#include "steam_ui.h"
#include "steam_prerequisites_ui.h"
#include "steam_settings.h"
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
static void artwork_draw(struct ui *,struct steam_artwork *,uint32_t,SDL_Rect,int,int);
static const char *download_state(enum steam_download_state state) {
  static const char *names[] = {"Idle",
                                "Finding Windows depots",
                                "Downloading manifests",
                                "Downloading / resuming files",
                                "Verifying files",
                                "Committing installation",
                                "Installed - compatibility unknown",
                                "Interrupted - partial files retained",
                                "Download failed",
                                "Cancelling - removing partial files",
                                "Cancelled - installation cache cleared"};
  return (unsigned int)state < sizeof(names) / sizeof(*names) ? names[state]
                                                              : "Unknown";
}
static const char *download_error(unsigned int e) {
  switch (e) {
  case 5:
    return "Not enough memory.";
  case 1009:
  case 1015:
  case 3003:
    return "License or depot access unavailable for this account.";
  case 3001:
  case 3002:
    return "Steam app metadata unavailable or invalid.";
  case 3004:
    return "No supported official HTTPS CDN server or token.";
  case 3101:
    return "SD filesystem error. Check the SD card.";
  case 3102:
    return "Insufficient free space on SD.";
  case 3103:
    return "Manifest or file integrity check failed. Retry to repair staging.";
  case 3104:
    return "Unsafe or unsupported installation path.";
  case 3105:
    return "Unsupported files, or the game is already installed.";
  case 3106:
    return "Steam CDN/network error. Check Wi-Fi and retry.";
  case 3107:
    return "Interrupted. Verified chunks will be reused when possible.";
  case 3110:
    return "Interrupted installation uses a different manifest plan. Preserve "
           "or remove its staging folder before starting a new version.";
  case 3112:
    return "Could not remove all partial files. Check the SD card and press Y to retry cleanup.";
  case 3111:
    return "Preferred language is unavailable. Press A to choose a language for this game.";
  default:
    return "Steam operation failed. Diagnostic log contains numeric error "
           "details.";
  }
}
static void bytes(char out[40], uint64_t n) {
  snprintf(out, 40, "%.2f %s",
           n >= UINT64_C(1073741824) ? (double)n / 1073741824
                                     : (double)n / 1048576,
           n >= UINT64_C(1073741824) ? "GiB" : "MiB");
}
static const char *integrity_text(unsigned int detail) {
  static const char *names[]={"Unspecified integrity failure","CDN response length",
    "Chunk decryption / padding","Chunk decompression","Manifest chunk checksum",
    "Manifest chunk SHA-1","File SHA-1 after SD write","Manifest metadata",
    "Chunk layout / coverage","Unsupported chunk size","Decoder memory allocation"};
  return detail<sizeof(names)/sizeof(*names)?names[detail]:names[0];
}
static void search_prompt(char out[128]) {
#ifdef __SWITCH__
  SwkbdConfig k;
  if (R_FAILED(swkbdCreate(&k, 0)))
    return;
  swkbdConfigMakePresetDefault(&k);
  swkbdConfigSetHeaderText(&k, "Search Steam library");
  swkbdConfigSetStringLenMax(&k, 127);
  swkbdShow(&k, out, 128);
  swkbdClose(&k);
#else
  (void)out;
#endif
}
static int register_game(struct ui *ui, const char *root,
                         const struct steam_game *g,
                         const struct steam_launcher_adapter *a) {
  char exe[512], base[512];
  int n;
  if (!a->register_executable) {
    ui_message(ui, "Steam", "Launcher adapter unavailable.");
    return 0;
  }
  if (!steam_install_executable(root, g->app_id, exe)) {
    ui_message(
        ui, "Choose executable",
        "Select the main .exe inside this game's installed Steam folder.");
    if (!a->choose_executable ||
        !a->choose_executable(a->context, exe, sizeof(exe)))
      return 0;
  }
  if (!steam_storage_directory(root, g->app_id, base))
    return 0;
  n = (int)strlen(base);
  if (n + 1 >= (int)sizeof(base))
    return 0;
  base[n++] = '/';
  base[n] = 0;
  if (n < 0 || (size_t)n >= sizeof(base) || strncmp(exe, base, (size_t)n) ||
      !steam_content_path(exe + n)) {
    ui_message(ui, "Steam",
               "Choose an executable inside the installed game folder.");
    return 0;
  }
  if (!a->register_executable(a->context, exe, g->name)) {
    ui_message(ui, "Steam",
               "Could not add executable to CorkDeck. Downloaded files were "
               "preserved.");
    return 0;
  }
  if(!steam_install_select_executable(root,g->app_id,exe))ui_toast(ui,"Added to CorkDeck; could not save Steam executable selection.",4000);
  ui_message(ui, "Added to CorkDeck",
             "Use Play and the existing per-game settings in CorkDeck's "
             "library. Compatibility is Unknown until tested. Steam or a "
             "third-party launcher may be required.");
  return 1;
}
static const char *store_status(enum store_state state)
{
    return state == STORE_PENDING ? "Reading Steam store..." : "Store data unavailable";
}

static void detail_row(struct ui *ui, int y, const char *label, const char *value)
{
    ui_text_fit(ui, ui->small, 438, y, 158, label, ui->dim, 0);
    ui_text_fit(ui, ui->small, 608, y, 576, value, ui->value, 1);
}

static int draw_compatibility(struct ui *ui, uint32_t app, int y, int show_specs)
{
    struct steam_compat_view view;
    steam_compat_status(app, &view);
    const struct steam_minimum *m = &view.minimum;
    struct steam_assessment assessment = view.community.count
        ? (struct steam_assessment){FIT_UNKNOWN, view.community.detail}
        : steam_compat_estimate(view.windows == STORE_READY ? m : NULL);
    static const struct { const char *label, *symbol; } badges[] = {
        [FIT_UNKNOWN]        = {"Switch estimate / Unknown", "?"},
        [FIT_PROMISING]      = {"Switch estimate / Worth trying", "+"},
        [FIT_TIGHT]          = {"Switch estimate / Limited headroom", "~"},
        [FIT_HIGH_RISK]      = {"Switch estimate / High risk", "!"},
    };
    enum steam_fit fit = assessment.fit;
    SDL_Color color = ui->dim;
    switch (fit) {
    case FIT_PROMISING: color = ui->success; break;
    case FIT_TIGHT: color = (SDL_Color){247, 194, 92, 255}; break;
    case FIT_HIGH_RISK: color = ui->danger; break;
    case FIT_UNKNOWN: break;
    }
    if (view.community.count) {
        if (!strcmp(view.community.stage, "启动失败")) color = ui->danger;
        else if (!strcmp(view.community.stage, "可持续游玩") || !strcmp(view.community.stage, "已通关")) color = ui->success;
        else color = (SDL_Color){109, 191, 245, 255};
    }
    int cy = y + TTF_FontHeight(ui->normal) / 2;
    ui_fill_circle(ui, 453, cy, 14, color);
    ui_text_centered(ui, ui->normal, 453, y, view.community.count ? "•" : badges[fit].symbol, ui->background);
    const char *label = assessment.reason ? badges[fit].label : "Switch estimate";
    char report_label[144];
    if (view.community.count) {
        snprintf(report_label, sizeof(report_label), "Switch 实测 · %s（%u 条）",
                 view.community.stage, view.community.count);
        label = report_label;
    }
    ui_text_fit(ui, ui->normal, 480, y, 704, label, ui->value, 0);
    y += TTF_FontHeight(ui->normal) + 8;
    if (assessment.reason) {
        int lines = ui_text_wrapped(ui, ui->small, 438, y, 746, 2, assessment.reason, ui->text, 0);
        y += lines * (TTF_FontHeight(ui->small) + 4);
    }

    if (!show_specs) return y;
    y += 14;
    ui_fill(ui, 438, y, 746, 1, ui->card);
    y += 14;
    ui_text(ui, ui->normal, 438, y, "Windows minimum", ui->value);
    y += TTF_FontHeight(ui->normal) + 6;
    const struct { const char *label, *value; } rows[] = {
        {"RAM", m->memory}, {"CPU", m->processor}, {"GPU", m->graphics}, {"DirectX", m->directx},
    };
    int first_row = y;
    if (view.windows == STORE_READY) {
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
            if (!rows[i].value[0]) continue;
            detail_row(ui, y, rows[i].label, rows[i].value);
            y += TTF_FontHeight(ui->small) + 6;
        }
    }
    if (first_row == y) {
        ui_text(ui, ui->small, 438, y, view.windows == STORE_READY ? "Not specified" : store_status(view.windows), ui->dim);
        y += TTF_FontHeight(ui->small) + 6;
    }
    return y;
}
static void report_compatibility(struct ui *ui, const struct steam_game *game)
{
    char url[256];
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], scratch[sizeof(code)];
    snprintf(url, sizeof(url), "https://github.com/lerosua/CorkDeck/issues/new?template=compatibility.yml&appid=%u&version=%s",
             game->app_id, CORKDECK_VERSION);
    if (!qrcodegen_encodeText(url, scratch, code, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
        return;
    const int modules = qrcodegen_getSize(code), scale = 300 / (modules + 8);
    const int side = (modules + 8) * scale, x = (1280 - side) / 2;
    ui_start_screen(ui);
    while (ui_begin_frame(ui)) {
        struct ui_input input;
        while (ui_poll(ui, &input))
            if (input.button == UI_B || (input.touch == UI_TOUCH_TAP && input.y < 80)) return;
        ui_background(ui);
        ui_header_back(ui, "Report compatibility", game->name);
        ui_fill(ui, x, 176, side, side, (SDL_Color){255,255,255,255});
        for (int j = 0; j < modules; ++j)
            for (int i = 0; i < modules; ++i)
                if (qrcodegen_getModule(code, i, j))
                    ui_fill(ui, x + (4+i)*scale, 176 + (4+j)*scale, scale, scale, (SDL_Color){0,0,0,255});
        ui_text_centered(ui, ui->normal, 640, 510, "Scan to report this game on GitHub", ui->value);
        const struct ui_hint hint = {UI_B, "Back"};
        ui_footer(ui, &hint, 1);
        ui_present(ui);
        ui_wait(ui);
    }
}
int steam_library_ui_game(struct ui *ui, struct steam_provider *p,
                         const char *root, const struct steam_game *g,
                         const struct steam_launcher_adapter *adapter,struct steam_artwork *art) {
  char text[768], size[40], language[32];
  int prerequisite_queued=0;
  const SDL_Rect action = g->installed ? (SDL_Rect){438, 456, 312, 58}
                                       : (SDL_Rect){84, 578, 300, 58};
  steam_compat_request(root, g->app_id, 0);
  struct steam_app_view app;
  struct steam_remove_view removal;
  ui_start_screen(ui);
  while (ui_begin_frame(ui)) {
    struct ui_input input;
    steam_provider_inspect(p, g->app_id, 0);
    steam_provider_app_status(p, &app);
    steam_provider_remove_status(p, &removal);
    steam_artwork_request(art,g->app_id);steam_artwork_tick(art,ui->renderer);
    while (ui_poll(ui, &input)) {
      SDL_Point touch = {input.x, input.y};
      if (input.touch == UI_TOUCH_TAP && SDL_PointInRect(&touch, &action)) input.button = UI_A;
      if (input.button == UI_B || (input.touch == UI_TOUCH_TAP && input.y < 80))
        goto done;
      if (input.button == UI_R) {
        report_compatibility(ui, g);
        ui_start_screen(ui);
        break;
      }
      if (input.button == UI_X && g->installed == 1) {
        struct steam_download_view download;
        steam_provider_download_status(p,&download);
        if(steam_download_active(download.state)||removal.busy){
          ui_message(ui,"Game dependencies","Finish the active download or removal first.");
        }else{
          prerequisite_queued=steam_prerequisites_ui(ui,root,g->app_id,
            app.app_id==g->app_id&&app.state==2?app.installscript:NULL,adapter);
          if(prerequisite_queued)goto done;
        }
        ui_start_screen(ui);break;
      }
      if (input.button == UI_PLUS) {
        steam_compat_request(root, g->app_id, 1);
        steam_provider_inspect(p, g->app_id, 1);
        break;
      }
      if (input.button == UI_A && !removal.busy) {
        if (g->installed == 2) {
          ui_toast(ui, "Removal is incomplete. Choose Delete game to finish.",
                   4000);
          break;
        }
        if (g->installed) {
          register_game(ui, root, g, adapter);
          goto done;
        }
        if (app.app_id != g->app_id || app.state != 2) {
          ui_toast(ui, "Wait for Steam metadata, or press + to retry.", 3000);
          break;
        }
        if (!app.windows) {
          ui_message(ui, "Steam",
                     "Steam does not list a supported Windows installation.");
          goto done;
        }
        const char *override = NULL;
        if(app.language_missing){
          /* As Steam does: a game that has no files in the chosen language
           * is installed in English when it has them. Only a game with
           * neither is asked about. Installing none of its language files
           * leaves it unable to start (Call of Duty 4 looks for
           * zone\<language> and finds no language at all). */
          if(app.english_available){
            strcpy(language,"english");
            ui_toast(ui,"This game has no files in the chosen language. Installing it in English.",4000);
          }else if(!steam_ui_install_language_picker(ui,app.available_languages,language)){
            ui_start_screen(ui);break;
          }
          override=language;
        }
        if (steam_provider_install_language(p, g->app_id, override)) {
          ui_toast(ui, app.english_fallback && !override
                           ? "This game has no files in the chosen language. Installing it in English."
                           : "Installation started. Open Downloads to view progress.",
                   4000);
          goto done;
        }
        ui_toast(ui,
                 "Another operation is active or the Steam account is offline.",
                 4000);
      }
      if (input.button == UI_Y && g->installed && !removal.busy) {
        snprintf(
            text, sizeof(text),
            "Delete %s and all files in its Steam installation folder? Saves "
            "or modifications inside that folder will also be deleted. Other "
            "games and the shared Wine runtime are preserved.",
            g->name);
        if (ui_confirm(ui, "Uninstall Steam game", text, "Delete game")) {
          if (!steam_provider_uninstall(p, g->app_id))
            ui_message(ui, "Steam",
                       "Cannot start uninstall. Finish the current download "
                       "and check the SD card.");
          else {
            ui_toast(ui, "Removing Steam installation...", 4000);
            goto done;
          }
        }
        ui_start_screen(ui);
      }
    }
    ui_background(ui);
    ui_header_back(ui,"Steam game",g->installed==2?"Removal pending":g->installed?"Installed":"Available to install");
    artwork_draw(ui, art, g->app_id, (SDL_Rect){84, g->installed ? 126 : 110, 300, 450}, 18, 255);
    int title_y = g->installed ? 142 : 106;
    int title_lines = ui_text_wrapped(ui, ui->large, 438, title_y, 746, 2, g->name, ui->value, 0);
    int metadata_y = 274;
    if (!g->installed) {
      int fit_y = title_y + title_lines * (TTF_FontHeight(ui->large) + 4) + 10;
      int specs_end = draw_compatibility(ui, g->app_id, fit_y, 1);
      ui_fill(ui, 438, specs_end + 12, 746, 1, ui->card);
      metadata_y = specs_end + 26;
    }

    int ready = app.app_id == g->app_id && app.state == 2;
    const char *install_size = "Reading Steam metadata...";
    if (ready) {
      bytes(size, app.size);
      install_size = app.language_missing ? "Choose language first" : app.size ? size : "Size unavailable";
    } else if (app.app_id == g->app_id && app.state == 3) {
      install_size = "Unavailable";
    }
    detail_row(ui, metadata_y, "Estimated size", install_size);
    steam_provider_language(p, language);
    const char *language_name = language;
    for (size_t i = 0; i < steam_language_count; ++i) {
      if (!strcmp(language, steam_languages[i].id)) {
        language_name = ui_translate(ui, steam_languages[i].name);
        break;
      }
    }
    const char *source = ui_translate(ui, g->shared ? "Steam Families" : "Owned library");
    if (ready && app.english_fallback)
      snprintf(text, sizeof(text), ui_translate(ui, "English (no files in %s) / %s"), language_name, source);
    else if (ready && app.language_missing)
      snprintf(text, sizeof(text), ui_translate(ui, "%s (choose alternative) / %s"), language_name, source);
    else
      snprintf(text, sizeof(text), "%s / %s", language_name, source);
    detail_row(ui, g->installed ? 328 : metadata_y + TTF_FontHeight(ui->small) + 6, "Game language", text);
    if (g->installed) {
      draw_compatibility(ui, g->app_id, 366, 0);
    }
    ui_rounded(ui,action.x,action.y,action.w,action.h,14,(SDL_Color){92,194,74,255});
    ui_text_centered(ui,ui->normal,action.x+action.w/2,action.y+13,g->installed?"Add to the library":"Install",(SDL_Color){6,32,10,255});
    if (removal.error && removal.app_id == g->app_id) {
      snprintf(text, sizeof(text),
               "Uninstall error %u. Retry Delete game; check SD permissions "
               "and filesystem.",
               removal.error);
      ui_text_fit(ui, ui->small, 84, 564, 1100, text, ui->danger, 0);
    }
    if (app.state == 3 && app.app_id == g->app_id) {
      snprintf(text, sizeof(text), "Steam metadata error %u", app.error);
      ui_text(ui, ui->small, g->installed ? 84 : 438, g->installed?594:609, text, ui->danger);
    }
    if (g->installed) {
      const struct ui_hint h[] = {{UI_A, "Add / Choose executable"},
                                  {UI_Y, "Delete game"},
                                  {UI_X, "Dependencies"}, {UI_R, "Report compatibility"},
                                  {UI_B, "Back"}};
      ui_footer(ui, h, 5);
    } else {
      const struct ui_hint h[] = {
          {UI_A, "Install"}, {UI_PLUS, "Retry metadata"}, {UI_R, "Report compatibility"}, {UI_B, "Back"}};
      ui_footer(ui, h, 4);
    }
    ui_present(ui);
    ui_wait(ui);
  }
done:
  ui_start_screen(ui);
  return prerequisite_queued;
}
static void artwork_draw(struct ui *ui, struct steam_artwork *art, uint32_t app,
                         SDL_Rect rect, int radius, int bright) {
  int w = 0, h = 0;
  SDL_Texture *texture = steam_artwork_texture(art, app, &w, &h);
  if (texture) {
    SDL_Rect source = {0, 0, w, h};
    /* Fill the card without stretching. Captions always remain outside art. */
    if ((int64_t)w * rect.h > (int64_t)h * rect.w) {
      source.w = h * rect.w / rect.h;
      source.x = (w - source.w) / 2;
    } else {
      source.h = w * rect.h / rect.w;
      source.y = (h - source.h) / 2;
    }
    ui_rounded_texture(ui, texture, &source, rect, radius,
                       (SDL_Color){bright, bright, bright, 255});
  } else
    ui_rounded(ui, rect.x, rect.y, rect.w, rect.h, radius, ui->card);
}
int steam_library_ui_page(struct ui *ui, struct steam_provider *p,
                          const char *root,
                          const struct steam_launcher_adapter *a,
                          int initial_tab) {
  enum {
    PAGE = 10,
    COLUMNS = 5,
    CARD = 200,
    PITCH = 228,
    TOP = 180,
    ROW = 240
  };
  struct steam_game rows[PAGE];
  struct steam_download_view d = {0};
  struct steam_library_view l = {0};
  struct steam_status_view auth;
  struct steam_artwork *art = steam_artwork_create(root);
  char search[128] = {0};
  size_t offset = 0, count = 0, total = 0;
  int selected = 0,
      tab = initial_tab >= 0 && initial_tab <= 2 ? initial_tab : 0,
      destination = -1;
  int header_focus = 0, header_selection = tab == 2 ? 3 : 2;
  Uint32 disk_tick = 0, rate_tick = 0;
  struct statvfs disk;
  uint64_t free = 0, rate_bytes = 0;
  uint32_t rate_app = 0;
  int free_known = 0;
  double install_rate = 0;
  ui_start_screen(ui);
  while (ui_begin_frame(ui)) {
    struct ui_input input;
    char text[512], size[40], done[40], space[40];
    size_t i;
    steam_provider_status(p, &auth);
    steam_provider_library_status(p, &l);
    steam_provider_download_status(p, &d);
    steam_artwork_pause(art, steam_download_active(d.state));
    steam_artwork_tick(art, ui->renderer);
    if (!disk_tick || SDL_GetTicks() - disk_tick >= 5000) {
      free_known = !statvfs(root, &disk);
      if (free_known)
        free = (uint64_t)disk.f_bavail * disk.f_frsize;
      disk_tick = SDL_GetTicks();
    }
    if (tab < 2) {
      count = tab ? steam_provider_installed_page(p, search, offset, rows, PAGE,
                                                  &total)
                  : steam_provider_library_page(p, search, offset, rows, PAGE,
                                                &total);
      if (!count && offset) {
        offset = 0;
        continue;
      }
      if (selected >= (int)count)
        selected = count ? (int)count - 1 : 0;
    }
    while (ui_poll(ui, &input)) {
      if (input.button == UI_B)
        goto finished;
      if (input.touch == UI_TOUCH_TAP && input.y < 80) {
        int hit =
            a->header_hit ? a->header_hit(a->context, input.x, input.y) : -1;
        if (hit == 2 || hit == 3) {
          tab = hit == 3 ? 2 : 0;
          header_selection = hit;
          offset = 0;
          selected = 0;
          break;
        }
        if (hit >= 0) {
          destination = hit;
          goto finished;
        }
        if (!a->header_hit)
          goto finished;
      }
      if (header_focus) {
        if (input.button == UI_LEFT && header_selection > 0)
          --header_selection;
        if (input.button == UI_RIGHT && header_selection < 4)
          ++header_selection;
        if (input.button == UI_DOWN)
          header_focus = 0;
        if (input.button == UI_A) {
          if (header_selection == 2 || header_selection == 3) {
            tab = header_selection == 3 ? 2 : 0;
            header_focus = 0;
          } else {
            destination = header_selection;
            goto finished;
          }
        }
        continue;
      }
      if (input.button == UI_L || input.button == UI_R) {
        tab = (tab + (input.button == UI_L ? 2 : 1)) % 3;
        header_selection = tab == 2 ? 3 : 2;
        offset = 0;
        selected = 0;
        break;
      }
      if (input.touch == UI_TOUCH_TAP && input.y >= 100 && input.y < 142) {
        if (input.x >= 84 && input.x < 620) {
          tab = input.x < 260 ? 0 : input.x < 440 ? 1 : 2;
          offset = 0;
          selected = 0;
          header_selection = tab == 2 ? 3 : 2;
          break;
        }
      }
      if (input.button == UI_PLUS) {
        steam_provider_refresh_library(p);
        break;
      }
      if (input.button == UI_X && tab < 2) {
        search_prompt(search);
        offset = 0;
        selected = 0;
        break;
      }
      if (tab == 2) {
        if (input.button == UI_UP) {
          header_focus = 1;
          header_selection = 3;
        }
        if (input.button == UI_Y &&
            ((d.state > STEAM_DOWNLOAD_IDLE && d.state < STEAM_DOWNLOAD_COMMITTING) ||
             d.state == STEAM_DOWNLOAD_CANCELLED || d.state == STEAM_DOWNLOAD_ERROR))
          steam_provider_cancel_download(p);
        if (input.button == UI_A && (d.state == STEAM_DOWNLOAD_CANCELLED ||
                                     d.state == STEAM_DOWNLOAD_ERROR || d.state == STEAM_DOWNLOAD_DISCARDED)) {
          if(d.error==3112)steam_provider_cancel_download(p);
          else if(d.error==3111){
            char language[32];
            if(steam_ui_install_language_picker(ui,d.available_languages,language))
              if(!steam_provider_install_language(p,d.app_id,language))ui_toast(ui,"Cannot retry while offline or another operation is active.",4000);
            ui_start_screen(ui);
          }
          else steam_provider_install(p, d.app_id);
        }
        continue;
      }
      if (input.button == UI_UP && selected < COLUMNS && !offset) {
        header_focus = 1;
        header_selection = 2;
        continue;
      }
      if (input.button == UI_UP || input.button == UI_DOWN ||
          input.button == UI_LEFT || input.button == UI_RIGHT) {
        int move = input.button == UI_UP     ? -COLUMNS
                   : input.button == UI_DOWN ? COLUMNS
                   : input.button == UI_LEFT ? -1
                                             : 1;
        int next = selected + move;
        if (next < 0 && offset) {
          offset = offset >= PAGE ? offset - PAGE : 0;
          selected = next + PAGE;
        } else if (next >= (int)count && offset + count < total) {
          offset += PAGE;
          selected = next - PAGE;
          if (selected < 0)
            selected = 0;
        } else if (next >= 0 && next < (int)count)
          selected = next;
        break;
      }
      if (input.touch == UI_TOUCH_TAP && input.y >= TOP &&
          input.y < TOP + 2 * ROW && input.x >= 84 && input.x < 1196) {
        int column = (input.x - 84) / PITCH, row = (input.y - TOP) / ROW;
        if ((input.x - 84) % PITCH >= CARD)
          continue;
        selected = row * COLUMNS + column;
        if (selected >= (int)count)
          continue;
        input.button = UI_A;
      }
      if ((input.button == UI_A || input.button == UI_Y) && count) {
        if(steam_library_ui_game(ui, p, root, &rows[selected], a,art)){destination=-2;goto finished;}
        steam_provider_download_status(p, &d);
        if (d.app_id == rows[selected].app_id &&
            steam_download_active(d.state))
          tab = 2;
        header_selection = tab == 2 ? 3 : 2;
        ui_start_screen(ui);
        break;
      }
    }
    if (tab < 2) {
      count = tab ? steam_provider_installed_page(p, search, offset, rows, PAGE,
                                                  &total)
                  : steam_provider_library_page(p, search, offset, rows, PAGE,
                                                &total);
      if (selected >= (int)count)
        selected = count ? (int)count - 1 : 0;
      /* Visible cards first. Requesting an entire large library would waste
       * memory, SD space and Wi-Fi; cached covers appear on later visits. */
      for (i = 0; i < count; ++i)
        steam_artwork_request(art, rows[i].app_id);
    }
    ui_background(ui);
    if (a->draw_header)
      a->draw_header(a->context,
                     header_focus ? header_selection
                     : tab == 2   ? 3
                                  : 2,
                     header_focus);
    else
      ui_header_back(ui, "Steam",
                     auth.state == STEAM_ONLINE ? "Connected"
                                                : "Account offline");
    {
      static const char *tabs[] = {"All games", "Installed", "Downloads"};
      for (i = 0; i < 3; ++i) {
        int x = 84 + (int)i * 180;
        ui_rounded(ui, x, 100, 164, 40, 12,
                   tab == (int)i ? ui->focus : ui->panel);
        if (tab == (int)i)
          ui_border(ui, x, 100, 164, 40, 1, ui->dim);
        ui_text_centered(ui, ui->small, x + 82, 109, tabs[i],
                         tab == (int)i ? ui->value : ui->dim);
      }
    }
    bytes(space, free);
    snprintf(text, sizeof(text), "SD free  %s",
             free_known ? space : "Unavailable");
    ui_text_right(ui, ui->small, 1196, 112, text, ui->dim);
    if (tab < 2) {
      snprintf(text, sizeof(text), "%zu games%s%s", total,
               search[0] ? "  /  " : "", search);
      ui_text_fit(ui, ui->small, 84, 149, 650, text, ui->dim, 0);
      if (count) {
        struct steam_app_view app;
        steam_provider_inspect(p, rows[selected].app_id, 0);
        steam_provider_app_status(p, &app);
        bytes(size, app.size);
        snprintf(text, sizeof(text), "Installation  %s%s",
                 app.app_id == rows[selected].app_id && app.state == 2
                     ? (app.size ? size : "Size unavailable")
                     : "Reading size...",
                 app.app_id == rows[selected].app_id && app.state == 2 &&
                         app.size
                     ? " (estimate)"
                     : "");
        ui_text_right(ui, ui->small, 1196, 149, text, ui->dim);
      }
      for (i = 0; i < count; ++i) {
        int x = 84 + (int)(i % COLUMNS) * PITCH,
            y = TOP + (int)(i / COLUMNS) * ROW,
            current = (int)i == selected && !header_focus;
        if (current)
          ui_animated_border(ui, x - 3, y - 3, CARD + 6, CARD + 6, 17, 3,
                             (SDL_Color){150, 160, 176, 90},
                             (SDL_Color){244, 247, 250, 255});
        artwork_draw(ui, art, rows[i].app_id, (SDL_Rect){x, y, CARD, CARD}, 14,
                     current ? 255 : 190);
        {
          int w = 0, h = 0;
          if (!steam_artwork_texture(art, rows[i].app_id, &w, &h)) {
            char letter[8] = {0};
            size_t n = 1;
            while ((rows[i].name[n] & 0xc0) == 0x80 && n < 4)
              ++n;
            memcpy(letter, rows[i].name, rows[i].name[0] ? n : 0);
            ui_text_centered(ui, ui->large, x + CARD / 2, y + 80, letter,
                             ui->dim);
          }
        }
        if (rows[i].installed) {
          ui_rounded(ui, x + 12, y + CARD - 34,
                     rows[i].installed == 2 ? 144 : 100, 24, 8,
                     (SDL_Color){0, 0, 0, 190});
          ui_text(ui, ui->small, x + 20, y + CARD - 33,
                  rows[i].installed == 2 ? "Removal pending" : "Installed",
                  rows[i].installed == 2 ? ui->danger : ui->success);
        }
        ui_text_fit(ui, ui->small, x, y + CARD + 6, CARD, rows[i].name,
                    current ? ui->value : ui->dim, current);
        ui_text(ui, ui->small, x, y + CARD + 25,
                rows[i].shared      ? "Steam Families"
                : rows[i].installed ? "In CorkDeck"
                                    : "Available to install",
                ui->dim);
      }
      if (!count)
        ui_text_wrapped(ui, ui->normal, ui->width / 2, 285, 850, 3,
                        l.state == STEAM_LIBRARY_LOADING
                            ? "Reading your Steam library..."
                        : tab ? "No installed Steam games yet."
                              : "No games found. Press + to refresh.",
                        ui->dim, 1);
      if (l.error || l.family_error || auth.error) {
        snprintf(text, sizeof(text),
                 "Steam error %u  /  + retries, Account is in Steam settings",
                 l.error          ? l.error
                 : l.family_error ? l.family_error
                                  : auth.error);
        ui_toast(ui, text, 1500);
      }
      {
        const struct ui_hint hints[] = {
            {UI_A, header_focus ? "Select" : "Game"},
            {UI_Y, "Options"},
            {UI_X, "Search"},
            {UI_L, "Tabs"},
            {UI_B, "Back"}};
        ui_footer(ui, hints, 5);
      }
    } else {
      Uint32 now = SDL_GetTicks();
      uint64_t eta = 0;
      if (rate_app != d.app_id || !rate_tick || d.downloaded < rate_bytes) {
        rate_app = d.app_id;
        rate_tick = now;
        rate_bytes = d.downloaded;
        install_rate = 0;
      }
      if (now - rate_tick >= 2000) {
        install_rate =
            (double)(d.downloaded - rate_bytes) * 1000 / (now - rate_tick);
        rate_tick = now;
        rate_bytes = d.downloaded;
      }
      ui_rounded(ui, 84, 194, 1112, 360, 18, ui->panel);
      snprintf(text, sizeof(text), "%s", download_state(d.state));
      ui_text_fit(ui, ui->large, 116, 222, 1030, text, ui->value, 0);
      ui_text_fit(ui, ui->small, 116, 276, 1030,
                  d.state==STEAM_DOWNLOAD_CLEANING ? "Removing unfinished files from this and previous attempts." :
                  d.state==STEAM_DOWNLOAD_DISCARDED ? "Temporary installation data removed from SD." :
                  d.app_id ? d.file
                           : "Select a game in Steam to start an installation.",
                  ui->dim, 0);
      ui_rounded(ui, 116, 326, 1048, 12, 6, ui->card);
      if (d.total) {
        double fraction = (double)d.downloaded / d.total;
        if (fraction > 1)
          fraction = 1;
        ui_rounded(ui, 116, 326, (int)(1048 * fraction), 12, 6, ui->selection);
      }
      bytes(done, d.downloaded);
      bytes(size, d.total);
      if(d.state==STEAM_DOWNLOAD_DISCARDED)
        snprintf(text,sizeof(text),"Cache cleared. Ready for a fresh installation.");
      else if(d.state==STEAM_DOWNLOAD_CLEANING)
        snprintf(text,sizeof(text),"Please wait for SD cleanup to finish.");
      else snprintf(text, sizeof(text), "%s / %s verified", done,
               d.total ? size : "Calculating size");
      ui_text(ui, ui->normal, 116, 368, text, ui->value);
      snprintf(text, sizeof(text),
               "Network  %.2f MiB/s    /    CDN requests  %u (peak %u)",
               d.bytes_per_second / 1048576, d.parallelism, d.peak_requests);
      if(d.state!=STEAM_DOWNLOAD_CLEANING&&d.state!=STEAM_DOWNLOAD_DISCARDED)
        ui_text(ui, ui->small, 116, 414, text, ui->dim);
      if (d.state == STEAM_DOWNLOAD_FILES && install_rate > 0 &&
          d.downloaded < d.total && d.elapsed_ms >= 10000)
        eta = (uint64_t)((d.total - d.downloaded) / install_rate);
      if (eta)
        snprintf(text, sizeof(text),
                 "Elapsed  %llu:%02llu    /    Remaining ~%llu:%02llu",
                 (unsigned long long)(d.elapsed_ms / 60000),
                 (unsigned long long)(d.elapsed_ms / 1000 % 60),
                 (unsigned long long)(eta / 60),
                 (unsigned long long)(eta % 60));
      else
        snprintf(text, sizeof(text),
                 "Elapsed  %llu:%02llu    /    Remaining time calculating",
                 (unsigned long long)(d.elapsed_ms / 60000),
                 (unsigned long long)(d.elapsed_ms / 1000 % 60));
      if(d.state!=STEAM_DOWNLOAD_CLEANING&&d.state!=STEAM_DOWNLOAD_DISCARDED)
        ui_text(ui, ui->small, 116, 450, text, ui->dim);
      if(!d.error&&d.state==STEAM_DOWNLOAD_FILES&&d.batch_files){
        snprintf(text,sizeof(text),"Small-file queue: %u verified files / %u buffers peak",d.batch_files,d.batch_peak_files);
        ui_text(ui,ui->small,116,490,text,ui->dim);
      }
      if (d.error) {
        snprintf(text, sizeof(text), "Error %u / HTTP %u / transport %u / cleanup errno %u",
                 d.error, d.http_status, d.transport, d.cleanup_errno);
        ui_text(ui, ui->small, 116, 490, text, ui->danger);
        if(d.error==3103){
          snprintf(text,sizeof(text),"Detail %u: %s / format %u / retries %u",d.integrity_detail,
                   integrity_text(d.integrity_detail),d.chunk_format,d.integrity_retries);
          ui_text_fit(ui,ui->small,116,525,1040,text,ui->danger,0);
        }
        ui_text_wrapped(ui, ui->small, 84, 577, 1100, 3,
                        download_error(d.error), ui->danger, 0);
      } else if (d.state == STEAM_DOWNLOAD_COMPLETE)
        ui_text_wrapped(
            ui, ui->normal, 84, 585, 1100, 2,
            "Installed. Open CorkDeck Library to Play with the existing game "
            "settings. Compatibility is Unknown until tested.",
            ui->dim, 0);
      if (d.diagnostic_error) {
        snprintf(
            text, sizeof(text),
            "Could not write logs/steam-download.log (filesystem error %u)",
            d.diagnostic_error);
        ui_text(ui, ui->small, 84, 650, text, ui->danger);
      }
      {
        const struct ui_hint hints[] = {{UI_A, d.error==3112 ? "Retry cleanup" : d.state==STEAM_DOWNLOAD_DISCARDED ? "Install again" : "Retry / Resume"},
                                        {UI_Y, d.state==STEAM_DOWNLOAD_CLEANING ? "Cleaning SD..." : d.state==STEAM_DOWNLOAD_COMMITTING ? "Finishing install" : "Cancel + clear cache"},
                                        {UI_L, "Tabs"},
                                        {UI_B, "Back"}};
        if(d.state==STEAM_DOWNLOAD_CLEANING||d.state==STEAM_DOWNLOAD_COMMITTING||
           d.state==STEAM_DOWNLOAD_COMPLETE||d.state==STEAM_DOWNLOAD_IDLE)
          ui_footer(ui,hints+2,2);
        else if(d.state==STEAM_DOWNLOAD_DISCARDED){
          const struct ui_hint cleared[]={{UI_A,"Install again"},{UI_L,"Tabs"},{UI_B,"Back"}};
          ui_footer(ui,cleared,3);
        } else ui_footer(ui, hints, 4);
      }
    }
    ui_present(ui);
    ui_wait(ui);
  }
finished:
  steam_artwork_destroy(art);
  ui_start_screen(ui);
  return destination;
}
void steam_library_ui_open(struct ui *ui, struct steam_provider *p,
                           const char *root,
                           const struct steam_launcher_adapter *a) {
  steam_library_ui_page(ui, p, root, a, 0);
}
