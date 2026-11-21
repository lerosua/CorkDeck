#ifndef WINE_NX_NETWORK_DIAG_CURL_H
#define WINE_NX_NETWORK_DIAG_CURL_H

#include <curl/curl.h>
#include "network_diag.h"

static inline void network_diag_curl( const char *stage, CURL *curl, CURLcode code )
{
    long http = 0, os_errno = 0, verify = 0;
    double dns = 0, tcp = 0, tls = 0, total = 0;

    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &http );
    curl_easy_getinfo( curl, CURLINFO_OS_ERRNO, &os_errno );
    curl_easy_getinfo( curl, CURLINFO_SSL_VERIFYRESULT, &verify );
    curl_easy_getinfo( curl, CURLINFO_NAMELOOKUP_TIME, &dns );
    curl_easy_getinfo( curl, CURLINFO_CONNECT_TIME, &tcp );
    curl_easy_getinfo( curl, CURLINFO_APPCONNECT_TIME, &tls );
    curl_easy_getinfo( curl, CURLINFO_TOTAL_TIME, &total );
    network_diag( "stage=%s curl=%d (%s) http=%ld errno=%ld verify=%ld dns_ms=%.0f tcp_ms=%.0f tls_ms=%.0f total_ms=%.0f",
                  stage, (int)code, curl_easy_strerror(code), http, os_errno, verify,
                  dns * 1000, tcp * 1000, tls * 1000, total * 1000 );
}
#endif
