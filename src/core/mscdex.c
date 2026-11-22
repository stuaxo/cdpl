/* mscdex.c - talk to MSCDEX and the CD-ROM driver through INT 2Fh/1510h */
#include <stdio.h>
#include <stdarg.h>
#include "mscdex.h"

mscdex_t cd;

static dosbuf_t io;                 /* request header + control block */
#define REQ_OFF 0
#define CB_OFF  64
#define IO_SIZE 512

static int ioctl_len[16] = { 5, 6, 0, 0, 9, 130, 5, 4, 5, 2, 7, 7, 11, 13, 11, 11 };

u32 rb_to_frame(u32 rb)
{
    return MSF((rb >> 16) & 0xFF, (rb >> 8) & 0xFF, rb & 0xFF);
}

u32 frame_to_rb(u32 frame)
{
    u32 m = frame / 4500UL, s = (frame / 75UL) % 60UL, f = frame % 75UL;
    return (m << 16) | (s << 8) | f;
}

void frame_msf(u32 frame, int *m, int *s, int *f)
{
    *m = (int)(frame / 4500UL);
    *s = (int)((frame / 75UL) % 60UL);
    *f = (int)(frame % 75UL);
}

const char *cmd_name(u8 cmd, u8 sub)
{
    static const char *in_names[16] = {
        "DevHdr", "HeadLoc", "Rsvd", "ErrStat", "ChanInfo", "DrvBytes",
        "DevStat", "SectSize", "VolSize", "MediaChg", "DiskInfo",
        "TrkInfo", "QInfo", "SubChan", "UPC", "AudStat" };
    static const char *out_names[6] = {
        "Eject", "Lock", "Reset", "ChanCtl", "CtlStr", "Close" };
    switch (cmd) {
    case CMD_IOCTL_IN:  return sub < 16 ? in_names[sub] : "IoctlIn?";
    case CMD_IOCTL_OUT: return sub < 6 ? out_names[sub] : "IoctlOut?";
    case CMD_READ_LONG: return "ReadLong";
    case CMD_SEEK:      return "Seek";
    case CMD_PLAY:      return "Play";
    case CMD_STOP:      return "Stop";
    case CMD_RESUME:    return "Resume";
    case 0xF0:          return "1508Read";
    case 0xF1:          return "MSCDEX";
    }
    return "?";
}

void cd_log(u8 cmd, u8 sub, u16 status, int cf, const char *fmt, ...)
{
    char note[40];
    cdlog_t *l;
    va_list ap;

    note[0] = 0;
    if (fmt) {
        va_start(ap, fmt);
        vsprintf(note, fmt, ap);
        va_end(ap);
    }
    /* a request that keeps failing the same way (a polled Q-channel
     * read, say) is one entry with a count, not a flood */
    if (cd.nlog) {
        l = &cd.log[(cd.nlog - 1) % CDLOG_N];
        if (l->cmd == cmd && l->sub == sub && l->status == status &&
            l->cf == cf && !strcmp(l->note, note)) {
            l->tick = bios_ticks();
            if (l->repeat < 0xFFFF)
                l->repeat++;
            return;
        }
    }
    l = &cd.log[cd.nlog % CDLOG_N];
    l->tick = bios_ticks();
    l->cmd = cmd;
    l->sub = sub;
    l->status = status;
    l->cf = cf;
    l->repeat = 0;
    strcpy(l->note, note);
    cd.nlog++;
}

/* "  12.3 QInfo    st=810C ERR DONE x23  note"; t0 = tick of time 0 */
void cd_log_format(const cdlog_t *l, u32 t0, char *b)
{
    u32 t = l->tick - t0;
    char rep[10];

    rep[0] = 0;
    if (l->repeat)
        sprintf(rep, " x%u", l->repeat + 1);
    sprintf(b, "%5lu.%lu %-8s st=%04X%s%s%s%s%s  %s", t / 18, (t % 18) * 10 / 18,
            cmd_name(l->cmd, l->sub), l->status,
            (l->status & ST_ERROR) ? " ERR" : "",
            (l->status & ST_BUSY) ? " BUSY" : "",
            (l->status & ST_DONE) ? " DONE" : "",
            l->cf ? " CF" : "", rep, l->note);
}

const cdlog_t *cd_log_get(unsigned back)
{
    if (back >= cd.nlog || back >= CDLOG_N)
        return 0;
    return &cd.log[(cd.nlog - 1 - back) % CDLOG_N];
}

/* ---- request plumbing ------------------------------------------------ */

static u8 FAR *req(void) { return io.p + REQ_OFF; }
static u8 FAR *cbp(void) { return io.p + CB_OFF; }

static void put16(u8 FAR *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put32(u8 FAR *p, u32 v) { put16(p, (u16)v); put16(p + 2, (u16)(v >> 16)); }
static u16 get16(const u8 FAR *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 get32(const u8 FAR *p) { return get16(p) | ((u32)get16(p + 2) << 16); }

static void req_begin(u8 len, u8 cmd)
{
    fmemset(req(), 0, 64);
    req()[0] = len;
    req()[2] = cmd;
}

/* send the header at io:REQ_OFF; returns the status word.  If INT 2Fh
 * itself fails (CF), returns ST_ERROR|0xFF. */
static u16 req_send(int *cfp)
{
    rmregs_t r;
    int cf;
    u16 st;

    rm_clear(&r);
    r.eax = 0x1510;
    r.ecx = (u32)cd.cur;
    r.es = io.seg;
    r.ebx = REQ_OFF;
    cf = rm_int(0x2F, &r);
    st = get16(req() + 3);
    if (cf)
        st |= ST_ERROR;
    if (cfp)
        *cfp = cf;
    cd.last_status = st;
    return st;
}

static u16 ioctl(u8 cmd, u8 code, u8 *cb, int len, int quiet)
{
    u16 st;
    int cf;

    if (len <= 0)
        len = code < 16 ? ioctl_len[code] : 1;
    req_begin(26, cmd);
    fmemset(cbp(), 0, 160);
    if (cb)
        fmemcpy(cbp(), cb, len);
    cbp()[0] = code;
    put16(req() + 14, CB_OFF);
    put16(req() + 16, io.seg);
    put16(req() + 18, (u16)len);
    st = req_send(&cf);
    if (cb)
        fmemcpy(cb, cbp(), len);
    if (!quiet || (st & ST_ERROR))
        cd_log(cmd, code, st, cf, 0);
    return st;
}

int cd_ioctl_in(u8 code, u8 *cb, int len) { return ioctl(CMD_IOCTL_IN, code, cb, len, 0); }
int cd_ioctl_out(u8 code, u8 *cb, int len) { return ioctl(CMD_IOCTL_OUT, code, cb, len, 0); }

/* ---- init ------------------------------------------------------------- */

int cd_init(void)
{
    rmregs_t r;
    int i;

    memset(&cd, 0, sizeof(cd));
    if (!io.seg && dos_alloc(&io, IO_SIZE))
        return -1;

    rm_clear(&r);
    r.eax = 0x1500;
    rm_int(0x2F, &r);
    cd.ndrives = (int)(r.ebx & 0xFFFF);
    if (cd.ndrives == 0)
        return -2;
    cd.cur = (int)(r.ecx & 0xFF);

    /* 150Bh must answer with the ADADh signature, otherwise whatever
     * replied to 1500h was not MSCDEX */
    rm_clear(&r);
    r.eax = 0x150B;
    r.ecx = (u32)cd.cur;
    rm_int(0x2F, &r);
    if ((u16)r.ebx != 0xADAD) {
        cd.ndrives = 0;
        return -2;
    }

    rm_clear(&r);
    r.eax = 0x150C;
    rm_int(0x2F, &r);
    cd.version = (u16)r.ebx;
    cd.present = 1;

    if (cd.ndrives > MAX_DRIVES)
        cd.ndrives = MAX_DRIVES;
    rm_clear(&r);
    r.eax = 0x150D;
    r.es = io.seg;
    r.ebx = CB_OFF;
    fmemset(cbp(), 0xFF, MAX_DRIVES);
    rm_int(0x2F, &r);
    for (i = 0; i < cd.ndrives; i++)
        cd.letters[i] = cbp()[i] < 26 ? cbp()[i] : (u8)(cd.cur + i);
    cd.cur = cd.letters[0];
    cd_log(0xF1, 0, 0, 0, "v%u.%02u %d drive(s) first %c:",
           cd.version >> 8, cd.version & 0xFF, cd.ndrives, 'A' + cd.cur);
    return cd_select(cd.cur);
}

void cd_exit(void) { dos_free(&io); }

int cd_select(int letter)
{
    rmregs_t r;
    u8 FAR *hdr;
    int i;

    cd.cur = letter;
    /* device header name via 1501h list */
    rm_clear(&r);
    r.eax = 0x1501;
    r.es = io.seg;
    r.ebx = CB_OFF;
    rm_int(0x2F, &r);
    strcpy(cd.devname, "?");
    for (i = 0; i < cd.ndrives; i++) {
        if (cd.letters[i] == letter) {
            u8 FAR *e = cbp() + i * 5;
            hdr = lowmem_ptr(get16(e + 3), get16(e + 1));
            fmemcpy(cd.devname, hdr + 10, 8);
            cd.devname[8] = 0;
            break;
        }
    }
    for (i = 7; i >= 0 && cd.devname[i] == ' '; i--)
        cd.devname[i] = 0;
    cd_read_toc();
    return 0;
}

/* ---- audio commands --------------------------------------------------- */

u16 cd_play(u32 from_frame, u32 nframes)
{
    u16 st;
    int cf;

    req_begin(22, CMD_PLAY);
    if (cd.redbook) {
        req()[13] = 1;
        put32(req() + 14, frame_to_rb(from_frame));
    } else {
        req()[13] = 0;
        put32(req() + 14, from_frame - 150);
    }
    put32(req() + 18, nframes);
    st = req_send(&cf);
    cd_log(CMD_PLAY, 0, st, cf, "%s %lu +%lu", cd.redbook ? "RB" : "HSG",
           from_frame - 150, nframes);
    return st;
}

static u16 simple(u8 cmd)
{
    u16 st;
    int cf;

    req_begin(13, cmd);
    st = req_send(&cf);
    cd_log(cmd, 0, st, cf, 0);
    return st;
}

u16 cd_stop(void) { return simple(CMD_STOP); }
u16 cd_resume(void) { return simple(CMD_RESUME); }

u16 cd_seek(u32 frame)
{
    u16 st;
    int cf;

    req_begin(24, CMD_SEEK);
    put32(req() + 20, frame - 150);
    st = req_send(&cf);
    cd_log(CMD_SEEK, 0, st, cf, "%lu", frame - 150);
    return st;
}

u16 cd_read_long(u32 frame, u16 n, u16 seg, u16 off, u8 mode)
{
    u16 st;
    int cf;

    req_begin(27, CMD_READ_LONG);
    req()[13] = 0;                      /* HSG */
    put16(req() + 14, off);
    put16(req() + 16, seg);
    put16(req() + 18, n);
    put32(req() + 20, frame - 150);
    req()[24] = mode;
    st = req_send(&cf);
    /* every digital block goes through here: only log errors and the
     * first few, otherwise the log scrolls too fast to read */
    if ((st & ST_ERROR) || cf)
        cd_log(CMD_READ_LONG, mode, st, cf, "%s %lu x%u", mode ? "raw" : "cooked",
               frame - 150, n);
    return st;
}

/* ---- IOCTL wrappers -------------------------------------------------- */

int cd_read_toc(void)
{
    u8 cb[16];
    int t;
    toc_t *toc = &cd.toc;

    memset(toc, 0, sizeof(*toc));
    if (cd_ioctl_in(10, cb, 7) & ST_ERROR)
        return -1;
    toc->first = cb[1];
    toc->last = cb[2];
    toc->leadout = rb_to_frame(get32(cb + 3));
    if (toc->first < 1 || toc->last >= MAX_TRACKS || toc->first > toc->last)
        return -1;
    for (t = toc->first; t <= toc->last; t++) {
        memset(cb, 0, sizeof(cb));
        cb[1] = (u8)t;
        if (ioctl(CMD_IOCTL_IN, 11, cb, 7, 1) & ST_ERROR)
            continue;
        toc->t[t].num = (u8)t;
        toc->t[t].start = rb_to_frame(get32(cb + 2));
        toc->t[t].ctrl = cb[6];
    }
    for (t = toc->first; t <= toc->last; t++) {
        u32 next = t < toc->last ? toc->t[t + 1].start : toc->leadout;
        toc->t[t].len = next > toc->t[t].start ? next - toc->t[t].start : 0;
    }
    toc->valid = 1;
    cd_log(CMD_IOCTL_IN, 11, cd.last_status, 0, "TOC %u-%u lead-out %lu",
           toc->first, toc->last, toc->leadout);
    return 0;
}

int track_is_audio(int t)
{
    if (!cd.toc.valid || t < cd.toc.first || t > cd.toc.last)
        return 0;
    return (cd.toc.t[t].ctrl & 0x40) == 0;
}

int cd_track_of(u32 frame)
{
    int t;

    if (!cd.toc.valid)
        return 0;
    for (t = cd.toc.last; t >= cd.toc.first; t--)
        if (frame >= cd.toc.t[t].start)
            return frame < cd.toc.leadout ? t : 0;
    return 0;
}

int cd_qinfo(qinfo_t *q)
{
    u8 cb[11];
    u16 st;

    memset(cb, 0, sizeof(cb));
    st = ioctl(CMD_IOCTL_IN, 12, cb, 11, 1);
    if (st & ST_ERROR)
        return -1;
    q->ctrl_adr = cb[1];
    q->tno = cb[2];
    q->index = cb[3];
    q->rm = cb[4]; q->rs = cb[5]; q->rf = cb[6];
    q->am = cb[8]; q->as = cb[9]; q->af = cb[10];
    q->abs = MSF(q->am, q->as, q->af);
    return st;
}

int cd_audstat(audstat_t *a)
{
    u8 cb[11];
    u16 st;

    memset(cb, 0, sizeof(cb));
    st = ioctl(CMD_IOCTL_IN, 15, cb, 11, 1);
    if (st & ST_ERROR)
        return -1;
    a->paused = get16(cb + 1);
    a->start = get32(cb + 3);
    a->end = get32(cb + 7);
    return st;
}

u32 cd_devstatus(void)
{
    u8 cb[5];

    memset(cb, 0, sizeof(cb));
    if (cd_ioctl_in(6, cb, 5) & ST_ERROR)
        return 0xFFFFFFFFUL;
    return get32(cb + 1);
}

int cd_sector_size(u8 mode)
{
    u8 cb[4];

    memset(cb, 0, sizeof(cb));
    cb[1] = mode;
    if (cd_ioctl_in(7, cb, 4) & ST_ERROR)
        return -1;
    return get16(cb + 2);
}

u32 cd_volsize(void)
{
    u8 cb[5];

    memset(cb, 0, sizeof(cb));
    if (cd_ioctl_in(8, cb, 5) & ST_ERROR)
        return 0;
    return get32(cb + 1);
}

int cd_media_changed(void)
{
    u8 cb[2];

    memset(cb, 0, sizeof(cb));
    if (cd_ioctl_in(9, cb, 2) & ST_ERROR)
        return -2;
    return (signed char)cb[1];
}

int cd_upc(char *out)
{
    u8 cb[11];
    int i;

    memset(cb, 0, sizeof(cb));
    if (cd_ioctl_in(14, cb, 11) & ST_ERROR) {
        strcpy(out, "n/a");
        return -1;
    }
    for (i = 0; i < 13; i++) {
        u8 b = cb[2 + i / 2];
        int d = (i & 1) ? (b & 15) : (b >> 4);
        out[i] = (char)(d < 10 ? '0' + d : '?');
    }
    out[13] = 0;
    return 0;
}

int cd_chaninfo(u8 *out8)
{
    u8 cb[9];

    memset(cb, 0, sizeof(cb));
    if (cd_ioctl_in(4, cb, 9) & ST_ERROR)
        return -1;
    memcpy(out8, cb + 1, 8);
    return 0;
}

int cd_set_volume(u8 left, u8 right)
{
    u8 cb[9];

    cb[0] = 3;
    cb[1] = 0; cb[2] = left;
    cb[3] = 1; cb[4] = right;
    cb[5] = 2; cb[6] = 0;
    cb[7] = 3; cb[8] = 0;
    return (cd_ioctl_out(3, cb, 9) & ST_ERROR) ? -1 : 0;
}

int cd_eject(void) { return (cd_ioctl_out(0, 0, 1) & ST_ERROR) ? -1 : 0; }
int cd_close_tray(void) { return (cd_ioctl_out(5, 0, 1) & ST_ERROR) ? -1 : 0; }
int cd_reset(void) { return (cd_ioctl_out(2, 0, 1) & ST_ERROR) ? -1 : 0; }

int cd_lock(int lock)
{
    u8 cb[2];

    cb[0] = 1;
    cb[1] = (u8)(lock ? 1 : 0);
    return (cd_ioctl_out(1, cb, 2) & ST_ERROR) ? -1 : 0;
}
