#include <assert.h>
#include <png.h>
#include <turbojpeg.h>
#include "../source/launcher_image.h"

int main( int argc, char **argv )
{
    struct launcher_icon image = {0}, decoded = {0};
    unsigned char *jpeg = NULL;
    size_t size = 0;
    assert( argc == 3 );
    assert( launcher_image_load( argv[1], &image ) );
    assert( launcher_image_square( &image ) );
    assert( launcher_image_jpeg( &image, &jpeg, &size ) );
    assert( size < 0x20000 && launcher_image_decode( jpeg, size, &decoded ) );
    assert( decoded.width == 256 && decoded.height == 256 );
    free( jpeg );
    launcher_icon_free( &image );
    launcher_icon_free( &decoded );
    assert( launcher_image_load( argv[2], &image ) );
    launcher_icon_free( &image );
    assert( !launcher_image_decode( NULL, 0, &image ) );
    assert( !launcher_image_decode( "not a png", 9, &image ) );
    assert( !launcher_image_decode( "invalid", 16u * 1024u * 1024u + 1, &image ) );
    image = (struct launcher_icon){ LAUNCHER_ICON_RGBA, 128, 32, calloc( 128 * 32, 4 ), 128 * 32 * 4 };
    assert( image.data );
    for (int i = 0; i < 128 * 32; i++)
    { image.data[i * 4] = 250; image.data[i * 4 + 3] = 128; }
    assert( launcher_image_square( &image ) );
    assert( image.width == 256 && image.height == 256 );
    assert( image.data[0] == 22 && image.data[1] == 27 && image.data[2] == 30 );
    assert( image.data[(128 * 256 + 128) * 4] > 120 && image.data[(128 * 256 + 128) * 4] < 150 );
    launcher_icon_free( &image );
    {
        png_image png = { .version = PNG_IMAGE_VERSION, .width = 2049, .height = 1, .format = PNG_FORMAT_RGBA };
        unsigned char pixels[2049 * 4] = {0};
        png_alloc_size_t bytes = 0;
        assert( png_image_write_to_memory( &png, NULL, &bytes, 0, pixels, 0, NULL ) );
        unsigned char *data = malloc( bytes );
        assert( data && png_image_write_to_memory( &png, data, &bytes, 0, pixels, 0, NULL ) );
        assert( !launcher_image_decode( data, bytes, &image ) );
        free( data );
    }
    /* Adobe YCCK stores inverted samples. Check the colour as well as decode success. */
    {
        unsigned char inverted[] = {255, 32, 0, 192};
        unsigned char *encoded = NULL;
        unsigned long length = 0;
        tjhandle encoder = tjInitCompress();
        assert( encoder && !tjCompress2( encoder, inverted, 1, 0, 1, TJPF_CMYK, &encoded, &length,
                                         TJSAMP_444, 100, 0 ) );
        assert( launcher_image_decode( encoded, length, &decoded ) );
        assert( decoded.width == 1 && decoded.height == 1 && abs(decoded.data[0] - 192) <= 2 &&
                abs(decoded.data[1] - 24) <= 2 && decoded.data[2] <= 2 && decoded.data[3] == 255 );
        launcher_icon_free( &decoded );
        tjFree( encoded );
        tjDestroy( encoder );
    }
    puts( "Forwarder icons: PNG/JPEG, bounded decode, alpha, aspect ratio and 256px JPEG passed" );
    return 0;
}
