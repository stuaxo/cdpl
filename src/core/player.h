/* player.h - transport over the analog (PLAY AUDIO) and digital
 * (raw READ LONG -> Sound Blaster) back ends */
#ifndef PLAYER_H
#define PLAYER_H
#include "plat.h"

enum { PS_STOPPED, PS_PLAYING, PS_PAUSED };
enum { MODE_ANALOG, MODE_DIGITAL };

#define SECT_PER_HALF 13
#define RAW_SECT      2352
#define RAW_HALF      (SECT_PER_HALF * RAW_SECT)   /* 30576 */

typedef struct {
    int   mode;
    int   state;
    int   ended;           /* set when a play range ran out */
    u32   from, to;        /* absolute frames, to is exclusive */
    u32   pos;             /* current absolute frame */
    int   track, index;
    u16   status;          /* last request status word */
    int   busy;
    u8    vol;             /* 0-255 */
    int   cd_mixer;        /* also set SB mixer CD line in analog */
    /* digital statistics */
    u32   blocks, underruns, read_errors;
    u32   peak_l, peak_r;  /* of the last block, 0-32767 */
    char  err[64];
} player_t;

extern player_t pl;

int   pl_init(void);
void  pl_exit(void);
int   pl_set_mode(int mode);
int   pl_play(u32 from, u32 to);
void  pl_pause(void);
void  pl_mark_playing(void);    /* analog PLAY/RESUME sent by the caller */
void  pl_resume(void);
void  pl_stop(void);
void  pl_poll(void);
void  pl_set_volume(u8 v);
void  pl_resync(void);          /* after start-up: is the drive playing? */
u32   pl_audio_end(u32 from);   /* end of the run of audio tracks */

#endif
