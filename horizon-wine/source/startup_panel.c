/* The normal compositor owns the display. Only upload pixels here, including
 * when the runtime's log flusher consumes a new startup stage. */
#include <SDL.h>
#include <SDL_ttf.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "compositor.h"
#include "startup_panel.h"

enum { PANEL_WIDTH = 960, PANEL_HEIGHT = 160 };
extern const unsigned char corkdeck_font[];
extern const size_t corkdeck_font_size;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct wine_nx_layer *layer;
static TTF_Font *font;
static SDL_Surface *surface;
static char title[144], previous_stage[128], previous_error[128];

static void text_row(const char *text, int y, SDL_Color color)
{
    SDL_Surface *row = TTF_RenderUTF8_Blended(font, text, color);
    SDL_Rect dest = {20, y, 0, 0};
    if (!row) return;
    SDL_BlitSurface(row, NULL, surface, &dest);
    SDL_FreeSurface(row);
}

void startup_panel_open(const char *program)
{
    pthread_mutex_lock(&lock);
    if (!TTF_Init()) {
        font = TTF_OpenFontRW(SDL_RWFromConstMem(corkdeck_font, (int)corkdeck_font_size), 1, 22);
        if (font) surface = SDL_CreateRGBSurfaceWithFormat(0, PANEL_WIDTH, PANEL_HEIGHT, 32, SDL_PIXELFORMAT_RGB888);
        if (surface) layer = wine_nx_layer_create(PANEL_WIDTH, PANEL_HEIGHT);
        if (layer) {
            snprintf(title, sizeof(title), "%s", program);
            wine_nx_layer_place_startup(layer, 32, 32);
        } else {
            SDL_FreeSurface(surface);
            surface = NULL;
            if (font) TTF_CloseFont(font);
            font = NULL;
            TTF_Quit();
        }
    }
    pthread_mutex_unlock(&lock);
}

int startup_panel_draw(const char *stage, const char *error)
{
    pthread_mutex_lock(&lock);
    int visible = wine_nx_startup_layer_visible(layer);
    if (visible && (strcmp(stage, previous_stage) || strcmp(error, previous_error))) {
        SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 26, 30, 37));
        text_row(title, 12, (SDL_Color){255, 255, 255, 255});
        text_row(stage, 55, (SDL_Color){180, 207, 233, 255});
        if (*error) text_row(error, 100, (SDL_Color){255, 184, 145, 255});
        wine_nx_layer_update(layer, surface->pixels, surface->pitch / 4, 0, 0, PANEL_WIDTH, PANEL_HEIGHT);
        snprintf(previous_stage, sizeof(previous_stage), "%s", stage);
        snprintf(previous_error, sizeof(previous_error), "%s", error);
    }
    pthread_mutex_unlock(&lock);
    return visible;
}

void startup_panel_close(void)
{
    pthread_mutex_lock(&lock);
    if (layer) {
        wine_nx_layer_destroy(layer);
        layer = NULL;
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);
        TTF_Quit();
        previous_stage[0] = previous_error[0] = 0;
    }
    pthread_mutex_unlock(&lock);
}
