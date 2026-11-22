/* state.h - CDPL.STA, handed between CDPL16 and CDPL32 on a switch */
#ifndef STATE_H
#define STATE_H
#include "plat.h"

typedef struct {
    int engine;         /* 16 or 32 */
    int mode;           /* MODE_ANALOG / MODE_DIGITAL */
    int drive;          /* letter, 0=A, -1 unknown */
    int state;          /* PS_* when the previous engine exited */
    u32 pos, from, to;
    int vol;
    int repeat, redbook, cd_mixer;
    int sel, focus;
    int mx, my;         /* pointer position in pixels, -1 unknown */
    int switched;       /* written by an engine switch */
} appstate_t;

void state_defaults(appstate_t *s);
int  state_load(const char *path, appstate_t *s);
int  state_save(const char *path, const appstate_t *s);

#endif
