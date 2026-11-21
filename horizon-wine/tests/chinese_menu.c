/* Check the locale selector, persisted preference and the font shipped in the NRO. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL_ttf.h>
#include "../source/app/app_settings.h"
#include "../source/app/app_version.h"
#include "../source/config_json.h"
#include "../source/shell/shell_strings.inc"

static void check_glyphs( TTF_Font *font, const char *text )
{
    Uint32 *chars = (Uint32 *)SDL_iconv_string( "UTF-32LE", "UTF-8", text, strlen(text) + 1 );
    assert( chars );
    for (size_t i = 0; chars[i]; i++)
    {
        Uint32 code = SDL_SwapLE32( chars[i] );
        if (code < 0x20) continue;
        if (!TTF_GlyphIsProvided32( font, code ))
        {
            fprintf( stderr, "Missing U+%04X in %s\n", (unsigned)code, text );
            abort();
        }
    }
    SDL_free( chars );
}

int main( int argc, char **argv )
{
    const char *const chinese[] = { "zh-CN", "zh-Hans", "zh-TW", "zh-Hant" };
    struct wine_nx_config config = {0};
    struct app_settings settings;
    const struct app_setting_def *language = app_setting_def( APP_SET_LANGUAGE );
    TTF_Font *font;

    assert( argc == 2 && TTF_Init() == 0 );
    font = TTF_OpenFont( argv[1], 24 );
    assert( font );
    for (size_t i = 0; i < sizeof(chinese) / sizeof(*chinese); i++)
        assert( shell_language_from_code( chinese[i] ) == APP_LANG_ZH_CN );
    assert( shell_language_from_code( "ru" ) == APP_LANG_RU );
    assert( shell_language_from_code( "en-US" ) == APP_LANG_EN );

    assert( wine_nx_config_parse( &config, "{\"language\":\"zh-CN\"}" ) );
    app_settings_resolve( &settings, &config, NULL, NULL, NULL, NULL );
    int choice = app_settings_get( &settings, APP_SET_LANGUAGE );
    assert( !strcmp( language->choices[choice].label[APP_LANG_EN], "简体中文" ) );
    shell_language = shell_language_from_code( language->choices[choice].value );
    assert( !strcmp( shell_translate( "Library" ), "游戏库" ) );
    assert( !strcmp( shell_translate( "First set-up" ), "首次设置" ) );
    for (size_t i = 0; i < sizeof(shell_strings) / sizeof(*shell_strings); i++)
    {
        const char *text = shell_strings[i][APP_LANG_ZH_CN];
        assert( text && *text && !strcmp( shell_translate( shell_strings[i][APP_LANG_EN] ), text ) );
        check_glyphs( font, text );
    }
    for (int i = 0; i < APP_SET_COUNT; i++)
    {
        const struct app_setting_def *def = app_setting_def( i );
        check_glyphs( font, def->name[APP_LANG_ZH_CN] );
        check_glyphs( font, def->purpose[APP_LANG_ZH_CN] );
        if (def->impact[APP_LANG_ZH_CN]) check_glyphs( font, def->impact[APP_LANG_ZH_CN] );
        for (int j = 0; j < def->choice_count; j++)
            check_glyphs( font, def->choices[j].label[APP_LANG_ZH_CN] );
    }
    TTF_CloseFont( font );
    TTF_Quit();
    puts( "Chinese console locales, saved language, menu translations and embedded font glyphs passed" );
    return 0;
}
