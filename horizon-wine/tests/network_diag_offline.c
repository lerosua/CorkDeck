#include "horizon_dlls_curl.h"
#include "steam/steam_probe.h"
#include <assert.h>
#include <curl/curl.h>
int main(void)
{
    struct horizon_dlls_transport transport = {0};
    struct horizon_dll_manifest manifest;
    struct steam_probe_result steam;
    struct steam_servers servers;
    unsigned long long size;

    assert( horizon_dlls_curl_open( &transport, 1 ) == 1 );
    assert( horizon_dlls_fetch_manifest( &transport, HORIZON_DLLS_MANIFEST_URL, NULL, 0,
                                         &manifest, NULL, NULL ) == HORIZON_DLLS_NETWORK );
    assert( transport.size( transport.opaque, HORIZON_DLLS_BUNDLE_URL, &size ) == HORIZON_DLLS_NETWORK );
    horizon_dlls_curl_close( &transport, 1 );

    steam_discover( &steam, &servers, NULL, NULL );
    assert( steam.code == STEAM_NETWORK && steam.transport_code == CURLE_COULDNT_RESOLVE_HOST );
    assert( !steam.http_status && !servers.count );
    return 0;
}
