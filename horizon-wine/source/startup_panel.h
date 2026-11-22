#ifndef CORKDECK_STARTUP_PANEL_H
#define CORKDECK_STARTUP_PANEL_H

void startup_panel_open(const char *program);
int startup_panel_draw(const char *stage, const char *error);
void startup_panel_close(void);

#endif
