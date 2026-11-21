#ifndef CORKDECK_STEAM_MANIFEST_TLS_H
#define CORKDECK_STEAM_MANIFEST_TLS_H

#include "../network_diag.h"
#include <string.h>

/* Curl's error buffer may contain request data. Only emit known categories. */
static inline const char *manifest_tls_detail(const char *error)
{
    if (strstr(error, "does not match") || strstr(error, "no alternative certificate") ||
        strstr(error, "not match") || strstr(error, "doesn't match")) return "hostname_mismatch";
    if (strstr(error, "expired")) return "expired_certificate";
    if (strstr(error, "not yet valid")) return "certificate_not_yet_valid";
    if (strstr(error, "revoked")) return "revoked_certificate";
    if (strstr(error, "issuer certificate") || strstr(error, "self-signed") ||
        strstr(error, "unknown CA")) return "untrusted_chain";
    return "unspecified_by_curl";
}

#ifdef __SWITCH__
#include <switch.h>

/* The devkitPro libnx curl backend queries this result before closing the
 * failed connection but doesn't publish it in CURLINFO_SSL_VERIFYRESULT.
 * Observe that very call, only on the worker currently fetching a manifest. */
static _Thread_local const char *manifest_tls_host;
Result __real_sslConnectionGetVerifyCertError(SslConnection *connection);

static const char *manifest_ssl_reason(Result rc)
{
    if (!rc) return "no_specific_error";
    if (R_MODULE(rc) != 123) return "service_error";
    switch (R_DESCRIPTION(rc)) {
    case 301: return "hostname_mismatch";
    case 302: return "missing_certificate";
    case 303: return "expired_certificate";
    case 304: return "revoked_certificate";
    case 305: return "unsupported_certificate";
    case 306: return "unknown_certificate";
    case 307: return "bad_certificate";
    case 308: return "unknown_ca";
    case 325: return "untrusted_oid";
    case 326: return "invalid_policy";
    default: return "other_ssl_result";
    }
}

Result __wrap_sslConnectionGetVerifyCertError(SslConnection *connection)
{
    Result rc = __real_sslConnectionGetVerifyCertError(connection);
    if (manifest_tls_host) {
        Result chain[8] = {0};
        u32 count = 0, total = 0;
        Result list = sslConnectionGetVerifyCertErrors(connection, &count, &total, chain, 8);
        network_diag("stage=steam.manifest.ssl host=%s result=0x%x module=%u description=%u reason=%s list_result=0x%x",
                     manifest_tls_host, rc, R_MODULE(rc), R_DESCRIPTION(rc), manifest_ssl_reason(rc), list);
        if (R_SUCCEEDED(list)) {
            for (u32 i = 0; i < count && i < 8; i++)
                network_diag("stage=steam.manifest.ssl.chain host=%s entry=%u result=0x%x reason=%s",
                             manifest_tls_host, i, chain[i], manifest_ssl_reason(chain[i]));
        }
    }
    return rc;
}
#endif
#endif
