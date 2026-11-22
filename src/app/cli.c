/* cli.c - command words for dumb consoles and scripts
 *
 *   CDPL PLAY 3 WAIT 5 PAUSE STATUS RESUME W 2 S
 *
 * Commands run in the order given.  Each prints one line starting with
 * "OK" or "ERROR"; the first error stops the run with exit code 1.
 * Switches (/x) are handled by main.c and may appear anywhere. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "plat.h"
#include "mscdex.h"
#include "player.h"
#include "sb.h"
#include "info.h"
#include "cli.h"
#include "cpu.h"

#include "selftest.h"

static char errmsg[100];

enum { C_PLAY, C_PAUSE, C_RESUME, C_STOP, C_SEEK, C_WAIT, C_INFO, C_TOC,
       C_STATUS, C_LOG, C_VOL, C_SBCD, C_TEST, C_EJECT, C_INSERT };
enum { ARG_NONE, ARG_OPT, ARG_REQ };

static const struct {
    const char *name, *abbr;
    int arg;
    const char *need;               /* what the argument is, for errors */
} cmds[] = {
    { "PLAY",   "P",  ARG_OPT,  0 },
    { "PAUSE",  "PA", ARG_NONE, 0 },
    { "RESUME", "R",  ARG_NONE, 0 },
    { "STOP",   "S",  ARG_NONE, 0 },
    { "SEEK",   "SK", ARG_REQ,  "a position, e.g. SEEK 3+01:00 or SEEK 12:30" },
    { "WAIT",   "W",  ARG_OPT,  0 },
    { "INFO",   "I",  ARG_NONE, 0 },
    { "TOC",    "T",  ARG_NONE, 0 },
    { "STATUS", "ST", ARG_NONE, 0 },
    { "LOG",    "L",  ARG_NONE, 0 },
    { "VOL",    "V",  ARG_REQ,  "a level 0-255, e.g. VOL 200" },
    { "SBCD",   "SB", ARG_REQ,  "a level 0-255, e.g. SBCD 255" },
    { "TEST",   "TE", ARG_NONE, 0 },
    { "EJECT",  "E",  ARG_NONE, 0 },
    { "INSERT", "IN", ARG_NONE, 0 },
    { 0, 0, 0, 0 } };

static int cmd_index(const char *w)
{
    int i;

    for (i = 0; cmds[i].name; i++)
        if (!stricmp(w, cmds[i].name) || !stricmp(w, cmds[i].abbr))
            return i;
    return -1;
}

/* arguments always start with a digit (track, time, seconds, level) and
 * command words never do */
int cli_match(int argc, char **argv, int i, int *used, char *err)
{
    int c = cmd_index(argv[i]);
    int has = i + 1 < argc && isdigit((u8)argv[i + 1][0]);

    if (c < 0)
        return 0;
    *used = 1;
    if (cmds[c].arg == ARG_NONE)
        return 1;
    if (has)
        *used = 2;
    else if (cmds[c].arg == ARG_REQ) {
        sprintf(err, "%s needs %s", cmds[c].name, cmds[c].need);
        return -1;
    }
    return 1;
}

/* digits only, no sign or junk; -1 if bad */
static long parse_level(const char *s, long max)
{
    long v = 0;

    if (!*s)
        return -1;
    for (; *s; s++) {
        if (!isdigit((u8)*s))
            return -1;
        v = v * 10 + (*s - '0');
        if (v > max)
            return -1;
    }
    return v;
}

/* "5" or "2.5" seconds -> ticks; 0 if bad */
static u32 parse_secs(const char *s)
{
    const char *p = s;
    double v;

    while (isdigit((u8)*p))
        p++;
    if (*p == '.') {
        p++;
        while (isdigit((u8)*p))
            p++;
    }
    if (p == s || *p)
        return 0;
    v = atof(s);
    if (v <= 0 || v > 36000)
        return 0;
    return (u32)(v * 18.2 + 0.5);
}

/* a key press, consumed */
static int key_hit(void)
{
    rmregs_t r;

    rm_clear(&r);
    r.eax = 0x0100;
    rm_int(0x16, &r);
    if (r.flags & 0x40)                 /* ZF: no key */
        return 0;
    rm_clear(&r);
    r.eax = 0x0000;
    rm_int(0x16, &r);
    return 1;
}

static const char *dev_error(u16 st)
{
    static const char *names[16] = {
        "write protect", "unknown unit", "drive not ready", "unknown command",
        "CRC error", "bad request length", "seek error", "unknown media",
        "sector not found", "out of paper", "write fault", "read fault",
        "general failure", "reserved", "reserved", "invalid disk change" };
    return names[st & 0x0F];
}

/* fail with the status word decoded */
static int fail_st(const char *what, u16 st)
{
    if (st & ST_ERROR)
        sprintf(errmsg, "%s: status %04X, error %02Xh %s", what, st, st & 0xFF,
                dev_error(st));
    else
        sprintf(errmsg, "%s: status %04X", what, st);
    return -1;
}

static int fail(const char *msg)
{
    strncpy(errmsg, msg, sizeof(errmsg) - 1);
    return -1;
}

/* ---- positions ---------------------------------------------------- */

/* "MM:SS", "MM:SS.FF" or "MM:SS:FF" -> frames; returns chars used */
static int parse_msf(const char *s, u32 *frames)
{
    const char *p = s;
    long m, sec, f = 0;
    char *e;

    m = strtol(p, &e, 10);
    if (e == p || *e != ':')
        return 0;
    p = e + 1;
    sec = strtol(p, &e, 10);
    if (e == p || sec > 59)
        return 0;
    p = e;
    if (*p == '.' || *p == ':') {
        f = strtol(p + 1, &e, 10);
        if (e == p + 1 || f > 74)
            return 0;
        p = e;
    }
    *frames = MSF(m, sec, f);
    return (int)(p - s);
}

static int check_track(int t)
{
    char b[60];

    if (!cd.toc.valid)
        return fail("no TOC (no disc?)");
    if (t < cd.toc.first || t > cd.toc.last) {
        sprintf(b, "track %d not on disc (%u-%u)", t, cd.toc.first, cd.toc.last);
        return fail(b);
    }
    return 0;
}

/* one end of a range: track, track+MSF offset, or absolute MSF.  For an
 * end point a bare track means the end of that track. */
static int parse_point(const char *s, int is_end, u32 *frame, const char **rest)
{
    char *e;
    long t;
    u32 off = 0;
    int n;

    n = parse_msf(s, frame);
    if (n) {
        *rest = s + n;
        return 0;
    }
    t = strtol(s, &e, 10);
    if (e == s)
        return fail("bad position: use TRACK, TRACK+MM:SS[.FF] or MM:SS[.FF]");
    if (check_track((int)t))
        return -1;
    if (*e == '+') {
        n = parse_msf(e + 1, &off);
        if (!n)
            return fail("bad offset after '+': use MM:SS[.FF]");
        e += 1 + n;
    }
    *frame = cd.toc.t[t].start + off;
    if (is_end && off == 0)
        *frame += cd.toc.t[t].len;
    *rest = e;
    return 0;
}

/* "" -> first audio track to the end of its run of audio tracks */
static int parse_range(const char *s, u32 *from, u32 *to)
{
    const char *rest;
    int t;
    char b[80];

    if (!cd.toc.valid)
        return fail("no TOC (no disc?)");
    if (!*s) {
        for (t = cd.toc.first; t <= cd.toc.last; t++)
            if (track_is_audio(t))
                break;
        if (t > cd.toc.last)
            return fail("disc has no audio tracks");
        *from = cd.toc.t[t].start;
        *to = pl_audio_end(*from);
        return 0;
    }
    if (parse_point(s, 0, from, &rest))
        return -1;
    if (*rest == '-') {
        if (parse_point(rest + 1, 1, to, &rest))
            return -1;
    } else {
        *to = pl_audio_end(*from);
    }
    if (*rest) {
        sprintf(b, "unexpected '%s' in position", rest);
        return fail(b);
    }
    if (*from < 150 || *from >= cd.toc.leadout)
        return fail("position outside the disc");
    if (*to > cd.toc.leadout)
        *to = cd.toc.leadout;
    if (*to <= *from)
        return fail("range ends before it starts");
    t = cd_track_of(*from);
    if (!track_is_audio(t)) {
        sprintf(b, "position is in track %d, a data track", t);
        return fail(b);
    }
    return 0;
}

/* ---- status --------------------------------------------------------- */

static void print_status(const char *tag)
{
    char a[12], r[12];

    if (pl.mode == MODE_DIGITAL) {
        msf_fmt(a, pl.pos, 1);
        printf("%s digital %s T%02d abs %s blocks %lu underruns %lu errors %lu"
               " peak %lu/%lu\n", tag,
               pl.state == PS_PLAYING ? "playing" : pl.state == PS_PAUSED ? "paused" : "stopped",
               pl.track, a, pl.blocks, pl.underruns, pl.read_errors,
               pl.peak_l, pl.peak_r);
    } else {
        qinfo_t q;
        audstat_t as;
        int st = cd_qinfo(&q);
        int ast = cd_audstat(&as);
        if (st < 0) {
            printf("%s analog Q-channel failed st=%04X %s\n", tag,
                   cd.last_status, dev_error(cd.last_status));
        } else {
            msf_fmt(r, MSF(q.rm, q.rs, q.rf), 1);
            msf_fmt(a, q.abs, 1);
            printf("%s analog T%02d (tno %02X) idx %02X rel %s abs %s st=%04X%s",
                   tag, cd_track_of(q.abs), q.tno, q.index, r, a, (u16)st,
                   (st & ST_BUSY) ? " BUSY" : "");
        }
        if (ast >= 0)
            printf(" paused=%u resume %08lX-%08lX\n", as.paused & 1, as.start, as.end);
        else
            printf(" audstat failed st=%04X\n", cd.last_status);
    }
}

static void print_toc(void)
{
    int t;
    char s[12], l[12];

    if (!cd.toc.valid) {
        printf("TOC   not available (status %04X)\n", cd.last_status);
        return;
    }
    for (t = cd.toc.first; t <= cd.toc.last; t++) {
        msf_fmt(s, cd.toc.t[t].start, 1);
        msf_fmt(l, cd.toc.t[t].len, 1);
        printf("TRACK %2d %s start %s (HSG %6lu) len %s ctrl %02X\n", t,
               track_is_audio(t) ? "audio" : "data ", s, cd.toc.t[t].start - 150,
               l, cd.toc.t[t].ctrl);
    }
    msf_fmt(s, cd.toc.leadout, 1);
    printf("LEADOUT  %s (HSG %lu)\n", s, cd.toc.leadout - 150);
}

static void print_log(unsigned last_n)
{
    unsigned i, n = cd.nlog < CDLOG_N ? cd.nlog : CDLOG_N;
    char b[100];

    if (last_n && last_n < n)
        n = last_n;
    for (i = n; i-- > 0;) {
        cd_log_format(cd_log_get(i), 0, b);
        printf("LOG %s\n", b + 8);
    }
}

/* WAIT       until play ends (either mode) or a key is pressed
 * WAIT secs  the same, but at most secs seconds
 * Prints a status line each second.  Digital play only runs while the
 * program does, so a script that plays digitally ends with a WAIT. */
static int do_wait(const char *v)
{
    u32 t0 = bios_ticks(), ticks = 0, next = 0, el;
    unsigned sec = 0;
    const char *why;

    if (*v) {
        ticks = parse_secs(v);
        if (!ticks)
            return fail("WAIT takes seconds, e.g. WAIT 5 or WAIT 2.5");
    } else if (pl.state != PS_PLAYING) {
        printf("OK    WAIT nothing playing\n");
        return 0;
    }
    for (;;) {
        pl_poll();
        el = bios_ticks() - t0;
        if (el >= next) {
            char tag[16];
            sprintf(tag, "WAIT %2us", sec++);
            print_status(tag);
            next = (u32)sec * 182 / 10;
        }
        if (ticks && el >= ticks) { why = "time up"; break; }
        if (pl.state == PS_STOPPED && pl.ended) { why = "end of range"; break; }
        if (!ticks && pl.state != PS_PLAYING) { why = "play stopped"; break; }
        if (key_hit()) { why = "stopped by key"; break; }
    }
    if (pl.mode == MODE_DIGITAL && pl.read_errors)
        return fail("digital: READ LONG errors during play (see LOG)");
    printf("OK    WAIT %s\n", why);
    return 0;
}

/* ---- commands --------------------------------------------------------- */

static int c_play(const char *v)
{
    u32 from, to;
    char a[12], b[12];
    u16 st;

    if (parse_range(v, &from, &to))
        return -1;
    msf_fmt(a, from, 1);
    msf_fmt(b, to, 1);
    if (pl.mode == MODE_ANALOG) {
        st = cd_play(from, to - from);
        if (st & ST_ERROR)
            return fail_st("PLAY AUDIO", st);
        pl.from = from;
        pl.to = to;
        pl.pos = from;
        pl_mark_playing();
        printf("OK    PLAY analog %s-%s (HSG %lu +%lu) %s status %04X\n", a, b,
               from - 150, to - from, cd.redbook ? "Red Book" : "HSG", st);
    } else {
        if (pl_play(from, to))
            return fail(pl.err);
        printf("OK    PLAY digital %s-%s (HSG %lu +%lu) %s\n", a, b, from - 150,
               to - from, sb.desc);
#ifdef CDPL16
        if (cpu_level() == CPU_8086)
            printf("NOTE  8086/8088: digital play may not keep up (watch underruns)\n");
#endif
    }
    return 0;
}

static int c_pause(void)
{
    u16 st;
    audstat_t as;

    if (pl.mode == MODE_DIGITAL) {
        if (pl.state != PS_PLAYING)
            return fail("digital: not playing in this run (use PLAY first)");
        pl_pause();
        printf("OK    PAUSE digital\n");
        return 0;
    }
    st = cd_stop();
    if (st & ST_ERROR)
        return fail_st("STOP AUDIO (pause)", st);
    if (cd_audstat(&as) < 0)
        return fail_st("Audio Status Info after pause", cd.last_status);
    if (!(as.paused & 1))
        return fail("STOP AUDIO accepted but Audio Status does not report paused"
                    " (was the drive playing?)");
    pl.state = PS_PAUSED;
    printf("OK    PAUSE analog status %04X, resume range %08lX-%08lX\n", st,
           as.start, as.end);
    return 0;
}

static int c_resume(void)
{
    u16 st;

    if (pl.mode == MODE_DIGITAL) {
        if (pl.state != PS_PAUSED)
            return fail("digital: not paused in this run");
        pl_resume();
        printf("OK    RESUME digital\n");
        return 0;
    }
    st = cd_resume();
    if (st & ST_ERROR)
        return fail_st("RESUME AUDIO", st);
    pl_mark_playing();
    printf("OK    RESUME analog status %04X%s\n", st, (st & ST_BUSY) ? " BUSY" : "");
    return 0;
}

static int c_stop(void)
{
    u16 st, st2 = 0;
    audstat_t as;

    if (pl.mode == MODE_DIGITAL) {
        pl_stop();
        printf("OK    STOP digital\n");
        return 0;
    }
    /* the first STOP of a playing drive only pauses it; a second one
     * clears the resume position */
    st = cd_stop();
    if (st & ST_ERROR)
        return fail_st("STOP AUDIO", st);
    if (cd_audstat(&as) >= 0 && (as.paused & 1)) {
        st2 = cd_stop();
        if (st2 & ST_ERROR)
            return fail_st("second STOP AUDIO", st2);
        if (cd_audstat(&as) >= 0 && (as.paused & 1))
            return fail("two STOP AUDIOs but Audio Status still reports paused");
    }
    pl.state = PS_STOPPED;
    printf("OK    STOP analog status %04X%s\n", st, st2 ? " (sent twice)" : "");
    return 0;
}

static int c_seek(const char *v)
{
    u32 from, to;
    char a[12];
    u16 st;
    int was = pl.state;

    if (parse_range(v, &from, &to))
        return -1;
    msf_fmt(a, from, 1);
    if (pl.mode == MODE_DIGITAL) {
        if (was == PS_STOPPED)
            return fail("digital: nothing playing in this run (use PLAY pos)");
        if (pl_play(from, to))
            return fail(pl.err);
        if (was == PS_PAUSED)
            pl_pause();
        printf("OK    SEEK digital to %s\n", a);
        return 0;
    }
    if (was == PS_STOPPED) {
        /* plain SEEK request: moves the head, plays nothing */
        st = cd_seek(from);
        if (st & ST_ERROR)
            return fail_st("SEEK", st);
        printf("OK    SEEK analog (request 131) to %s status %04X\n", a, st);
        return 0;
    }
    st = cd_play(from, to - from);
    if (st & ST_ERROR)
        return fail_st("PLAY AUDIO (seek)", st);
    if (was == PS_PAUSED) {
        st = cd_stop();
        if (st & ST_ERROR)
            return fail_st("STOP AUDIO (stay paused after seek)", st);
    }
    printf("OK    SEEK analog by PLAY AUDIO to %s%s\n", a,
           was == PS_PAUSED ? ", paused again" : "");
    return 0;
}

static int c_vol(const char *v)
{
    long n = parse_level(v, 255);

    if (n < 0)
        return fail("VOL takes a level 0-255");
    if (cd_set_volume((u8)n, (u8)n))
        return fail_st("Audio Channel Control", cd.last_status);
    pl.vol = (u8)n;
    if (pl.mode == MODE_DIGITAL)
        sb_set_voice_volume((u8)n, (u8)n);
    printf("OK    VOL %ld (Audio Channel Control%s)\n", n,
           pl.mode == MODE_DIGITAL ? " + SB voice" : "");
    return 0;
}

/* tray: IOCTL output 0 (eject) / 5 (close tray), then the door bit of
 * Device Status (bit 0 set = open) */
static int c_tray(int close)
{
    u32 ds;

    if (!close && pl.mode == MODE_DIGITAL && pl.state != PS_STOPPED)
        pl_stop();                      /* we are the one reading */
    if (close ? cd_close_tray() : cd_eject())
        return fail_st(close ? "Close Tray (IOCTL output 5)" : "Eject (IOCTL output 0)",
                       cd.last_status);
    ds = cd_devstatus();
    if (!close) {
        pl.state = PS_STOPPED;
        printf("OK    EJECT status %04X, door %s\n", cd.last_status,
               (ds & 1) ? "open" : "reported closed");
        return 0;
    }
    cd_read_toc();
    if (cd.toc.valid)
        printf("OK    INSERT status %04X, door %s, TOC %u-%u\n", cd.last_status,
               (ds & 1) ? "reported open" : "closed", cd.toc.first, cd.toc.last);
    else
        printf("OK    INSERT status %04X, door %s, no TOC (no disc?)\n",
               cd.last_status, (ds & 1) ? "reported open" : "closed");
    return 0;
}

/* the SB mixer's CD input, raw 0-255 (SB16 uses the top 5 bits, SB Pro
 * the top 4) */
static int c_sbcd(const char *v)
{
    long n = parse_level(v, 255);

    if (n < 0)
        return fail("SBCD takes a level 0-255");
    if (!sb.present)
        return fail(sb.desc);
    sb_set_cd_volume((u8)n, (u8)n);
    printf("OK    SBCD %ld (SB mixer CD input)\n", n);
    return 0;
}

int cli_run(int argc, char **argv, int init_rc)
{
    int i, rc = 0, used, c;
    const char *v;
    char err[80];

    if (init_rc < 0) {
        printf("ERROR %s\n", init_rc == -2 ? "MSCDEX is not installed (or reports no CD drives)"
                                           : "cannot allocate DOS memory");
        return EXIT_NOCD;
    }
    printf("CDPL%d %s  MSCDEX %u.%02u  drive %c: (%s)  mode %s\n", ENGINE_BITS,
           plat_name(), cd.version >> 8, cd.version & 0xFF, 'A' + cd.cur,
           cd.devname, pl.mode == MODE_ANALOG ? "analog" : "digital");
    if (pl.mode == MODE_ANALOG)
        pl_resync();                /* is the drive already playing/paused? */

    /* main.c has checked the whole line already */
    for (i = 1; i < argc && rc == 0; i += used) {
        used = 1;
        if (argv[i][0] == '/' || argv[i][0] == '-')
            continue;
        if (cli_match(argc, argv, i, &used, err) <= 0)
            continue;
        c = cmd_index(argv[i]);
        v = used == 2 ? argv[i + 1] : "";
        errmsg[0] = 0;
        switch (c) {
        case C_INFO: {
            char l[INFO_MAX][INFO_W];
            int n = info_lines(l, INFO_MAX), k;
            for (k = 0; k < n; k++)
                printf("INFO  %s\n", l[k]);
            print_toc();
            break;
        }
        case C_TOC:    print_toc(); break;
        case C_STATUS: print_status("STATUS"); break;
        case C_LOG:    print_log(0); break;
        case C_PLAY:   rc = c_play(v); break;
        case C_PAUSE:  rc = c_pause(); break;
        case C_RESUME: rc = c_resume(); break;
        case C_STOP:   rc = c_stop(); break;
        case C_SEEK:   rc = c_seek(v); break;
        case C_WAIT:   rc = do_wait(v); break;
        case C_VOL:    rc = c_vol(v); break;
        case C_SBCD:   rc = c_sbcd(v); break;
        case C_EJECT:  rc = c_tray(0); break;
        case C_INSERT: rc = c_tray(1); break;
        case C_TEST:
            if (selftest_run(1))
                rc = fail("self test failed (see FAIL lines)");
            break;
        }
        if (rc) {
            printf("ERROR %s: %s\n", cmds[c].name, errmsg);
            printf("-- last requests --\n");
            print_log(8);
        }
    }
    if (pl.mode == MODE_DIGITAL && pl.state != PS_STOPPED)
        printf("NOTE  digital play stops when the program exits (end with WAIT)\n");
    return rc ? EXIT_FAIL : EXIT_OK;
}
