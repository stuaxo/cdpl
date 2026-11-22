/* player.c - analog and digital CD audio back ends */
#include <stdio.h>
#include "player.h"
#include "cpu.h"
#include "mscdex.h"
#include "sb.h"

player_t pl;

static dmabuf_t dma;
static dosbuf_t stage;
static int      dig_ready;
static u32      out_half;          /* bytes per DMA half */
static u32      next_sect;         /* next frame to read */
static u32      half_start[2];     /* first frame held in each half */
static u16      consumed;          /* IRQs handled */
static u32      last_poll;
static u32      play_tick;

int pl_init(void)
{
    memset(&pl, 0, sizeof(pl));
    pl.vol = 255;
    return 0;
}

static void dig_free(void)
{
    if (dma.p)
        dma_free(&dma);
    if (stage.p)
        dos_free(&stage);
    dig_ready = 0;
}

void pl_exit(void)
{
    if (pl.state != PS_STOPPED && pl.mode == MODE_DIGITAL)
        sb_stop();
    dig_free();
}

u32 pl_audio_end(u32 from)
{
    int t = cd_track_of(from);

    if (!t)
        return cd.toc.leadout;
    while (t < cd.toc.last && track_is_audio(t + 1))
        t++;
    return t < cd.toc.last ? cd.toc.t[t + 1].start : cd.toc.leadout;
}

/* ---- digital --------------------------------------------------------- */

static int dig_setup(void)
{
    if (dig_ready)
        return 0;
    if (!sb.present) {
        sprintf(pl.err, "Digital needs a Sound Blaster: %s", sb.desc);
        return -1;
    }
    if (sb.type == SB_16 && sb.bits16)
        out_half = RAW_HALF;
    else if (sb.type == SB_16)
        out_half = RAW_HALF / 2;                /* 8-bit stereo 44k */
    else if (sb.stereo)
        out_half = RAW_HALF / 4;                /* 8-bit stereo 22k */
    else
        out_half = RAW_HALF / 8;                /* 8-bit mono 22k */
    if (dma_alloc(&dma, out_half * 2)) {
        strcpy(pl.err, "Cannot allocate DMA buffer");
        return -1;
    }
    if (out_half != RAW_HALF && dos_alloc(&stage, RAW_HALF)) {
        dma_free(&dma);
        strcpy(pl.err, "Cannot allocate staging buffer");
        return -1;
    }
    dig_ready = 1;
    return 0;
}

static void peaks(const s16 FAR *s, u32 n)
{
    u32 i;
    u32 l = 0, r = 0;

    for (i = 0; i < n; i += 2 * 16) {       /* sample sparsely: cheap */
        s32 a = s[i], b = s[i + 1];
        if (a < 0) a = -a;
        if (b < 0) b = -b;
        if ((u32)a > l) l = (u32)a;
        if ((u32)b > r) r = (u32)b;
    }
    pl.peak_l = l;
    pl.peak_r = r;
}

/* convert 16-bit stereo 44.1k in src to the card format at dst */
static void convert(u8 FAR *dst, const s16 FAR *src, u32 frames)
{
    u32 i;

    if (sb.type == SB_16) {                 /* 8-bit stereo 44k */
        for (i = 0; i < frames * 2; i++)
            dst[i] = (u8)((src[i] >> 8) + 128);
    } else if (sb.stereo) {                 /* 8-bit stereo 22k */
        for (i = 0; i < frames / 2; i++) {
            s32 l = ((s32)src[i * 4] + src[i * 4 + 2]) >> 1;
            s32 r = ((s32)src[i * 4 + 1] + src[i * 4 + 3]) >> 1;
            dst[i * 2] = (u8)((l >> 8) + 128);
            dst[i * 2 + 1] = (u8)((r >> 8) + 128);
        }
    } else {                                /* 8-bit mono 22k */
        for (i = 0; i < frames / 2; i++) {
            s32 m = ((s32)src[i * 4] + src[i * 4 + 1] +
                     src[i * 4 + 2] + src[i * 4 + 3]) >> 2;
            dst[i] = (u8)((m >> 8) + 128);
        }
    }
}

static void fill_half(int h)
{
    u8 FAR *dst = dma.p + (u32)h * out_half;
    u16 seg, off;
    u8 FAR *raw;
    u16 n, st;
    u8 silence = (sb.type == SB_16 && sb.bits16) ? 0 : 0x80;

    if (next_sect >= pl.to) {
        fmemset(dst, silence, (unsigned)out_half);
        half_start[h] = pl.to;
        return;
    }
    n = (u16)((pl.to - next_sect) < SECT_PER_HALF ? pl.to - next_sect
                                                   : SECT_PER_HALF);
    if (out_half == RAW_HALF) {
        seg = (u16)(dma.seg + (u16)((h * out_half) >> 4));
        off = 0;
        raw = dst;
    } else {
        seg = stage.seg;
        off = 0;
        raw = stage.p;
    }
    st = cd_read_long(next_sect, n, seg, off, 1);
    pl.status = st;
    pl.blocks++;
    if (st & ST_ERROR) {
        pl.read_errors++;
        fmemset(raw, 0, RAW_HALF);
    } else if (n < SECT_PER_HALF) {
        fmemset(raw + (u32)n * RAW_SECT, 0, (unsigned)((SECT_PER_HALF - n) * RAW_SECT));
    }
    peaks((const s16 FAR *)raw, (u32)RAW_HALF / 2);
    if (out_half != RAW_HALF)
        convert(dst, (const s16 FAR *)raw, (u32)RAW_HALF / 4);
    half_start[h] = next_sect;
    next_sect += n;
}

static int dig_start(u32 from)
{
    if (dig_setup())
        return -1;
#ifdef CDPL16
    if (cpu_level() == CPU_8086)
        cd_log(0xF1, 0, 0, 0, "8086: digital play may not keep up");
#endif
    next_sect = from;
    pl.blocks = pl.underruns = pl.read_errors = 0;
    fill_half(0);
    fill_half(1);
    consumed = 0;
    if (sb_start(&dma, out_half * 2)) {
        strcpy(pl.err, "Sound Blaster start failed");
        return -1;
    }
    sb_set_voice_volume(pl.vol, pl.vol);
    return 0;
}

static void dig_poll(void)
{
    u16 irqs = sb_irq_count;
    u16 diff = (u16)(irqs - consumed);
    int playing;
    u32 rem, done, total = out_half * 2, in_half;

    if (diff >= 2) {
        pl.underruns++;
        consumed = (u16)(irqs - 1);
    }
    if (consumed != irqs) {
        fill_half(consumed & 1);
        consumed++;
    }
    playing = irqs & 1;
    rem = sb_dma_remaining();
    done = rem <= total ? total - rem : 0;
    in_half = done >= (u32)playing * out_half ? done - (u32)playing * out_half : 0;
    if (in_half > out_half)
        in_half = out_half;
    if (half_start[playing] >= pl.to) {
        sb_stop();
        pl.state = PS_STOPPED;
        pl.ended = 1;
        pl.pos = pl.to;
        return;
    }
    pl.pos = half_start[playing] + in_half * SECT_PER_HALF / out_half;
    if (pl.pos >= pl.to)
        pl.pos = pl.to - 1;
}

/* ---- analog ---------------------------------------------------------- */

static void ana_poll(void)
{
    qinfo_t q;
    int st = cd_qinfo(&q);

    if (st < 0) {
        pl.status = cd.last_status;
        return;
    }
    pl.status = (u16)st;
    pl.busy = (st & ST_BUSY) != 0;
    pl.index = q.index;
    if (q.abs)
        pl.pos = q.abs;
    if (pl.state == PS_PLAYING && !pl.busy &&
        bios_ticks() - play_tick > TICKS_PER_SEC) {
        pl.state = PS_STOPPED;
        pl.ended = 1;
    }
}

/* ---- common ---------------------------------------------------------- */

int pl_play(u32 from, u32 to)
{
    u16 st;

    if (pl.state != PS_STOPPED)
        pl_stop();
    pl.err[0] = 0;
    pl.ended = 0;
    pl.from = from;
    pl.to = to;
    pl.pos = from;
    pl.track = cd_track_of(from);
    play_tick = bios_ticks();
    if (pl.mode == MODE_ANALOG) {
        st = cd_play(from, to - from);
        pl.status = st;
        if (st & ST_ERROR) {
            sprintf(pl.err, "PLAY AUDIO failed, status %04X", st);
            return -1;
        }
        pl_set_volume(pl.vol);
    } else if (dig_start(from)) {
        return -1;
    }
    pl.state = PS_PLAYING;
    return 0;
}

/* the caller sent PLAY AUDIO or RESUME itself (command line): track it
 * as playing so the poll can see the end of play */
void pl_mark_playing(void)
{
    pl.state = PS_PLAYING;
    pl.ended = 0;
    play_tick = bios_ticks();
}

void pl_pause(void)
{
    if (pl.state != PS_PLAYING)
        return;
    if (pl.mode == MODE_ANALOG) {
        cd_stop();
    } else if (sb.highspeed) {
        sb_stop();                       /* SB Pro high speed: no pause */
    } else {
        sb_pause();
    }
    pl.state = PS_PAUSED;
}

void pl_resume(void)
{
    u16 st;

    if (pl.state != PS_PAUSED)
        return;
    if (pl.mode == MODE_ANALOG) {
        st = cd_resume();
        if (st & ST_ERROR) {
            sprintf(pl.err, "RESUME failed, status %04X", st);
            return;
        }
        play_tick = bios_ticks();
    } else if (sb.highspeed) {
        pl.state = PS_STOPPED;
        pl_play(pl.pos, pl.to);
        return;
    } else {
        sb_continue();
    }
    pl.state = PS_PLAYING;
}

void pl_stop(void)
{
    if (pl.mode == MODE_ANALOG) {
        cd_stop();
        if (pl.state == PS_PLAYING)
            cd_stop();                   /* first one only pauses */
    } else if (pl.state != PS_STOPPED) {
        sb_stop();
    }
    pl.state = PS_STOPPED;
}

int pl_set_mode(int mode)
{
    u32 pos = pl.pos, to = pl.to;
    int was = pl.state;

    if (mode == pl.mode)
        return 0;
    if (was != PS_STOPPED)
        pl_stop();
    pl.mode = mode;
    if (was != PS_STOPPED) {
        if (pl_play(pos, to))
            return -1;
        if (was == PS_PAUSED)
            pl_pause();
    }
    return 0;
}

void pl_poll(void)
{
    u32 now = bios_ticks();

    if (pl.mode == MODE_DIGITAL) {
        if (pl.state == PS_PLAYING)
            dig_poll();
    } else if (pl.state != PS_STOPPED &&
               (now - last_poll >= 3 || now < last_poll)) {
        /* only while playing or paused: a stopped drive may legitimately
         * refuse Q-channel reads (dosemu: 810Ch), and polling would just
         * repeat that error */
        last_poll = now;
        ana_poll();
    }
    pl.track = cd_track_of(pl.pos);
}

void pl_set_volume(u8 v)
{
    pl.vol = v;
    if (pl.mode == MODE_ANALOG) {
        cd_set_volume(v, v);
        if (pl.cd_mixer)
            sb_set_cd_volume(v, v);
    } else {
        sb_set_voice_volume(v, v);
    }
}

/* the previous engine may have left the drive playing (analog) */
void pl_resync(void)
{
    audstat_t a;
    qinfo_t q;
    int st;

    if (pl.mode != MODE_ANALOG)
        return;
    st = cd_qinfo(&q);
    if (st < 0)
        return;
    if (q.abs)
        pl.pos = q.abs;
    if (st & ST_BUSY) {
        pl.state = PS_PLAYING;
        play_tick = bios_ticks();
    } else if (cd_audstat(&a) >= 0 && (a.paused & 1)) {
        pl.state = PS_PAUSED;
    } else {
        pl.state = PS_STOPPED;
    }
    pl.track = cd_track_of(pl.pos);
}
