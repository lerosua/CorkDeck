#ifndef WINE_NX_NETWORK_DIAG_H
#define WINE_NX_NETWORK_DIAG_H

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#ifdef __SWITCH__
void wine_nx_runtime_trace( const char *msg );
void network_diag_snapshot( const char *stage );
#else
static inline void network_diag_snapshot( const char *stage ) { (void)stage; }
#endif

/* Log only diagnostics, validated CDN hostnames and bounded public store fields.
 * Never log request URLs, headers or auth data. */
static inline void network_diag( const char *format, ... ) __attribute__((format(printf, 1, 2)));
static inline void network_diag( const char *format, ... )
{
    char message[384], line[448];
    va_list args;

    va_start( args, format );
    vsnprintf( message, sizeof(message), format, args );
    va_end( args );
    snprintf( line, sizeof(line), "[NETDIAG] time=%lld %s", (long long)time(NULL), message );
#ifdef __SWITCH__
    wine_nx_runtime_trace( line );
#else
    fprintf( stderr, "%s\n", line );
#endif
}
#endif
