#include "../launcher_ui.h"
#include "../launcher_image.h"

/* Three 2:3 posters side by side in one embedded JPEG, shared by both libraries. */
extern const unsigned char corkdeck_default_covers[];
extern const size_t corkdeck_default_covers_size;

void ui_default_cover( struct ui *ui, unsigned int app_id, const char *title,
                       SDL_Rect rect, int radius, int brightness )
{
    static const int posters = 3;
    unsigned int key = app_id;
    int width, height;
    if (!ui->default_covers_tried)
    {
        struct launcher_icon image = {0};
        ui->default_covers_tried = 1;
        if (launcher_image_decode( corkdeck_default_covers, corkdeck_default_covers_size, &image ))
        {
            SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom( image.data, image.width, image.height,
                                              32, image.width * 4, SDL_PIXELFORMAT_RGBA32 );
            if (surface)
            {
                ui->default_covers = SDL_CreateTextureFromSurface( ui->renderer, surface );
                SDL_FreeSurface( surface );
            }
            launcher_icon_free( &image );
        }
    }
    if (!ui->default_covers || SDL_QueryTexture( ui->default_covers, NULL, NULL, &width, &height ) < 0)
    {
        ui_rounded( ui, rect.x, rect.y, rect.w, rect.h, radius, ui->card );
        return;
    }

    if (!app_id)
        for (const unsigned char *c = (const unsigned char *)title; *c; ++c) key = key * 31 + *c;
    int panel = width / posters;
    SDL_Rect src = { (int)(key % posters) * panel, 0, panel, height };
    if (panel * rect.h > height * rect.w)
    {
        src.w = height * rect.w / rect.h;
        src.x += (panel - src.w) / 2;
    }
    else
    {
        src.h = panel * rect.h / rect.w;
        src.y = (height - src.h) / 2;
    }
    ui_rounded_texture( ui, ui->default_covers, &src, rect, radius,
                        (SDL_Color){brightness, brightness, brightness, 255} );
}
