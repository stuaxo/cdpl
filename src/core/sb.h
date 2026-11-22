/* sb.h - Sound Blaster DSP / mixer / ISA DMA */
#ifndef SB_H
#define SB_H
#include "plat.h"

typedef struct {
    int  present;
    u16  base;
    int  irq, dma8, dma16;
    u8   dsp_major, dsp_minor;
    int  type;               /* SB_* */
    /* chosen output format */
    u16  rate;
    int  stereo, bits16;
    int  highspeed;          /* SB Pro high speed DMA (no pause) */
    char desc[48];
} sbcard_t;

enum { SB_NONE, SB_20, SB_PRO, SB_16 };

extern sbcard_t sb;
extern volatile u16 sb_irq_count;

int   sb_detect(void);            /* parse BLASTER, reset DSP, version */
int   sb_reset_dsp(void);
void  sb_write(u8 v);
int   sb_read(void);
void  sb_mixer(u8 reg, u8 val);
u8    sb_mixer_read(u8 reg);

/* start auto-init playback of buf (size bytes, two halves); an IRQ
 * comes at the end of each half */
int   sb_start(dmabuf_t *buf, u32 size);
void  sb_stop(void);
void  sb_pause(void);
void  sb_continue(void);
u32   sb_dma_remaining(void);     /* bytes left in the current cycle */
void  sb_set_voice_volume(u8 l, u8 r);    /* 0-255 */
void  sb_set_cd_volume(u8 l, u8 r);

#endif
