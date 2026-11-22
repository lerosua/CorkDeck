#ifndef CORKDECK_ELECTRON_PROBE_H
#define CORKDECK_ELECTRON_PROBE_H

#include <stdint.h>

void wine_nx_probe_set_window_phase( int phase );
void wine_nx_probe_message( const char *source, const char *line );
void wine_nx_probe_seh( const char *msg );
unsigned int wine_nx_probe_snapshot( const char *reason, unsigned int code, uintptr_t stack );

#endif
