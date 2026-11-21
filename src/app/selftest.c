/* selftest.c - TEST command: non-interactive checks, plain text output */
#include <stdio.h>
#include "plat.h"
#include "mscdex.h"
#include "sb.h"
#include "player.h"
#include "selftest.h"

static int fails, passes;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) passes++; else fails++;
}

static void wait_ticks(u32 n)
{
    u32 t0 = bios_ticks();
    while (bios_ticks() - t0 < n)
        pl_poll();
}

static int first_audio(void)
{
    int t;
    for (t = cd.toc.first; t <= cd.toc.last; t++)
        if (track_is_audio(t))
            return t;
    return 0;
}

static int first_data(void)
{
    int t;
    for (t = cd.toc.first; t <= cd.toc.last; t++)
        if (!track_is_audio(t))
            return t;
    return 0;
}

static void dump_log(void)
{
    unsigned i, n = cd.nlog < CDLOG_N ? cd.nlog : CDLOG_N;

    printf("-- request log (oldest first) --\n");
    for (i = n; i-- > 0;) {
        char b[100];
        cd_log_format(cd_log_get(i), 0, b);
        printf("  %s\n", b + 8);
    }
}

int selftest_run(int verbose)
{
    int t, a, d;
    dosbuf_t sec;
    u32 p0, p1, p2;
    qinfo_t q;
    u16 st;

    (void)verbose;

    printf("CDPL%d self test, %s\n", ENGINE_BITS, plat_name());
    check(cd.present, "MSCDEX installed");
    if (!cd.present) {
        printf("RESULT FAIL\n");
        return 1;
    }
    printf("     MSCDEX %u.%02u, %d drive(s), using %c: (%s)\n", cd.version >> 8,
           cd.version & 0xFF, cd.ndrives, 'A' + cd.cur, cd.devname);
    printf("     device status %08lX, sector size cooked %d raw %d\n",
           cd_devstatus(), cd_sector_size(0), cd_sector_size(1));
    check(cd.toc.valid, "TOC read (Audio Disk Info / Track Info)");
    if (cd.toc.valid) {
        for (t = cd.toc.first; t <= cd.toc.last; t++) {
            int m, s, f;
            frame_msf(cd.toc.t[t].start, &m, &s, &f);
            printf("     track %2d %s %02d:%02d.%02d ctrl %02X len %lu\n", t,
                   track_is_audio(t) ? "audio" : "data ", m, s, f,
                   cd.toc.t[t].ctrl, cd.toc.t[t].len);
        }
    }
    a = first_audio();
    d = first_data();
    check(a != 0, "disc has an audio track");
    if (!a) {
        dump_log();
        printf("RESULT FAIL\n");
        return 1;
    }

    /* ---- analog ---- */
    pl.mode = MODE_ANALOG;
    if (pl_play(cd.toc.t[a].start, pl_audio_end(cd.toc.t[a].start)) != 0) {
        check(0, "PLAY AUDIO accepted");
        printf("SKIP analog checks: %s\n", pl.err);
    } else {
        int s0, s1;
        check(1, "PLAY AUDIO accepted");
        wait_ticks(6);
        s0 = cd_qinfo(&q);
        p0 = q.abs;
        check(s0 >= 0, "Q-channel info");
        check(s0 >= 0 && (s0 & ST_BUSY), "busy bit set while playing");
        wait_ticks(TICKS_PER_SEC * 2);
        s1 = cd_qinfo(&q);
        p1 = q.abs;
        printf("     Q-channel %lu -> %lu (%ld frames in ~2 s)\n", p0, p1, (long)(p1 - p0));
        check(s0 >= 0 && s1 >= 0 && p1 > p0 + 75 && p1 < p0 + 300,
              "Q-channel advances while playing");
        pl_pause();
        wait_ticks(TICKS_PER_SEC);
        s0 = cd_qinfo(&q);
        p1 = q.abs;
        wait_ticks(TICKS_PER_SEC);
        s1 = cd_qinfo(&q);
        p2 = q.abs;
        check(s0 >= 0 && s1 >= 0 && p2 == p1, "pause freezes the head");
        check(s1 >= 0 && !(s1 & ST_BUSY), "not busy while paused");
        {
            audstat_t as;
            check(cd_audstat(&as) >= 0 && (as.paused & 1), "audio status reports paused");
        }
        pl_resume();
        wait_ticks(TICKS_PER_SEC);
        s1 = cd_qinfo(&q);
        check(s1 >= 0 && q.abs > p2 && (s1 & ST_BUSY), "resume continues");
        pl_stop();
        wait_ticks(4);
        s1 = cd_qinfo(&q);
        check(s1 >= 0 && !(s1 & ST_BUSY), "stop clears busy");
    }

    /* ---- raw reads ---- */
    if (dos_alloc(&sec, 2352 * 2) == 0) {
        u32 i, nz = 0;
        fmemset(sec.p, 0, 2352);
        st = cd_read_long(cd.toc.t[a].start + 75 * 2, 1, sec.seg, 0, 1);
        for (i = 0; i < 2352; i++)
            if (sec.p[i])
                nz++;
        printf("     raw READ LONG status %04X, %lu non-zero bytes\n", st, nz);
        check(!(st & ST_ERROR), "raw READ LONG of an audio sector");
        check(nz > 0, "audio sector is not silence");
        if (d) {
            st = cd_read_long(cd.toc.t[d].start + 16, 1, sec.seg, 0, 0);
            check(!(st & ST_ERROR), "cooked READ LONG of a data sector");
        }
        dos_free(&sec);
    }

    /* ---- digital ---- */
    if (sb.present) {
        printf("     %s\n", sb.desc);
        pl.mode = MODE_DIGITAL;
        if (pl_play(cd.toc.t[a].start, pl_audio_end(cd.toc.t[a].start)) == 0) {
            wait_ticks(TICKS_PER_SEC * 2);
            printf("     %lu blocks, %lu underruns, %lu read errors, %u IRQs, pos %lu\n",
                   pl.blocks, pl.underruns, pl.read_errors, sb_irq_count,
                   pl.pos - cd.toc.t[a].start);
            check(sb_irq_count >= 4, "Sound Blaster IRQs arrive");
            check(pl.read_errors == 0, "digital reads succeed");
            check(pl.pos > cd.toc.t[a].start + 75, "digital position advances");
            pl_stop();
        } else {
            check(0, pl.err);
        }
    } else {
        printf("SKIP digital playback: %s\n", sb.desc);
    }

    if (verbose && fails)
        dump_log();
    printf("RESULT %s (%d passed, %d failed)\n", fails ? "FAIL" : "PASS", passes, fails);
    return fails ? 1 : 0;
}
