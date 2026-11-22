/* mscdex.h - MSCDEX INT 2Fh AH=15h interface and CD device requests */
#ifndef MSCDEX_H
#define MSCDEX_H
#include "plat.h"

#define MAX_TRACKS 100
#define MAX_DRIVES 26

/* request header status word bits */
#define ST_ERROR 0x8000
#define ST_BUSY  0x0200
#define ST_DONE  0x0100

/* device request command codes */
#define CMD_IOCTL_IN   3
#define CMD_IOCTL_OUT 12
#define CMD_READ_LONG 128
#define CMD_SEEK      131
#define CMD_PLAY      132
#define CMD_STOP      133
#define CMD_RESUME    136

typedef struct {
    u8  num;
    u8  ctrl;         /* CONTROL/ADR byte */
    u32 start;        /* absolute frame, MSF counting (sector + 150) */
    u32 len;          /* frames */
} cdtrack_t;

typedef struct {
    int       valid;
    u8        first, last;
    u32       leadout;          /* absolute frame of the lead-out */
    cdtrack_t t[MAX_TRACKS];    /* indexed by track number */
} toc_t;

typedef struct {
    u8  ctrl_adr, tno, index;
    u8  rm, rs, rf;             /* running time in track */
    u8  am, as, af;             /* running time on disc */
    u32 abs;                    /* absolute frame (am,as,af) */
} qinfo_t;

typedef struct {
    u16 paused;
    u32 start, end;             /* Red Book, as returned */
} audstat_t;

/* one entry of the request log, shown in the TUI */
typedef struct {
    u32  tick;
    u8   cmd, sub;              /* command code, IOCTL control code */
    u16  status;
    int  cf;                    /* INT 2Fh returned carry */
    u16  repeat;                /* identical requests folded in */
    char note[40];
} cdlog_t;

#define CDLOG_N 64

typedef struct {
    int       present;
    u16       version;          /* BH.BL */
    int       ndrives;
    u8        letters[MAX_DRIVES];
    int       cur;              /* drive letter, 0=A */
    char      devname[9];
    u16       last_status;
    toc_t     toc;
    cdlog_t   log[CDLOG_N];
    unsigned  nlog;             /* total ever logged */
    int       redbook;          /* use Red Book addressing for PLAY */
} mscdex_t;

extern mscdex_t cd;

int   cd_init(void);
void  cd_exit(void);
int   cd_select(int letter);

int   cd_ioctl_in(u8 code, u8 *cb, int len);   /* returns status word */
int   cd_ioctl_out(u8 code, u8 *cb, int len);
u16   cd_play(u32 from_frame, u32 nframes);
u16   cd_stop(void);
u16   cd_resume(void);
u16   cd_seek(u32 frame);
/* raw read of n sectors starting at absolute frame into a low-memory
 * buffer at seg:off; mode 1 raw (2352), 0 cooked (2048) */
u16   cd_read_long(u32 frame, u16 n, u16 seg, u16 off, u8 mode);
/* cooked read via 1508h into the internal buffer, return pointer */

int   cd_read_toc(void);
int   cd_qinfo(qinfo_t *q);
int   cd_audstat(audstat_t *a);
u32   cd_devstatus(void);
int   cd_sector_size(u8 mode);
u32   cd_volsize(void);
int   cd_media_changed(void);
int   cd_upc(char *out14);
int   cd_chaninfo(u8 *out8);
int   cd_set_volume(u8 left, u8 right);
int   cd_eject(void);
int   cd_close_tray(void);
int   cd_lock(int lock);
int   cd_reset(void);

int   cd_track_of(u32 frame);       /* track containing frame, 0 if none */
int   track_is_audio(int t);

void  cd_log(u8 cmd, u8 sub, u16 status, int cf, const char *fmt, ...);
const cdlog_t *cd_log_get(unsigned back);   /* 0 = newest */
void  cd_log_format(const cdlog_t *l, u32 t0, char *buf);  /* >= 96 bytes */

/* Red Book helpers */
#define MSF(m, s, f) ((u32)(m) * 4500UL + (u32)(s) * 75UL + (u32)(f))
u32   rb_to_frame(u32 rb);       /* 00MMSSFF (binary) -> frame */
u32   frame_to_rb(u32 frame);
void  frame_msf(u32 frame, int *m, int *s, int *f);

const char *cmd_name(u8 cmd, u8 sub);

#endif
