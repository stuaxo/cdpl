/* ui.h - the full-screen player (main screen, menus, dialogs) */
#ifndef UI_H
#define UI_H
#include "plat.h"

/* runs the player until exit; init_rc is cd_init()'s result, t0 the tick
 * the log times count from.  Returns the exit code (16/32 = switch). */
int  ui_run(int init_rc, u32 t0);
void ui_break(void);        /* Ctrl-C / Ctrl-Break: leave the loop */
void ui_shutdown(void);     /* restore the screen if the UI is up */

#endif
