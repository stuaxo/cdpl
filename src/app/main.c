/* main.c - CDPL16 / CDPL32 entry: switches, start-up, and the hand-off to
 * the command words (cli.c), or the full-screen player (ui.c) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include "plat.h"
#include "mscdex.h"
#include "sb.h"
#include "player.h"
#include "opts.h"
#include "cli.h"
#include "ui.h"

opts_t opt = { 0, 0, 0, 0, -1, -1, 0, 0, "CDPL.STA", "" };

static void on_break(int sig)
{
    (void)sig;
    ui_break();
    signal(SIGINT, on_break);
    signal(SIGBREAK, on_break);
}

static void cleanup(void)
{
    ui_shutdown();
    pl_exit();
    sb_stop();
    plat_exit();
}

static void usage(void)
{
    printf("CDPL - CD audio test player for MSCDEX  (this is CDPL%d, %s)\n"
           "CDPL [switches] [command ...]       no command: the full-screen player\n"
           "\n"
           "Commands run in order, one OK or ERROR line each; the first error stops:\n"
           "  P  PLAY [pos[-end]]  pos = TRACK, TRACK+MM:SS[.FF] or MM:SS[.FF]\n"
           "  PA PAUSE   R RESUME   S STOP   SK SEEK pos\n"
           "  W  WAIT [secs]       until play ends, or secs pass; a key stops it\n"
           "  E  EJECT   IN INSERT        open / close the tray\n"
           "  I  INFO   T TOC   ST STATUS   L LOG (MSCDEX requests)\n"
           "  V  VOL 0-255         Audio Channel Control (+ SB voice when digital)\n"
           "  SB SBCD 0-255        SB mixer CD input level\n"
           "  TE TEST              self test, PASS/FAIL lines\n"
           "\n"
           "Switches, anywhere on the line:\n"
           "  /M:A /M:D   /MODE:ANALOG|DIGITAL  PLAY AUDIO, or raw READ LONG to the SB\n"
           "  /D:x        /DRIVE:x       CD drive x:\n"
           "  /RB         /REDBOOK       PLAY AUDIO with Red Book addresses\n"
           "  /16 /32                    engine (CDPL.EXE runs the one asked for)\n"
           "  /NF /NOFONT  /BF /BIOSFONT  /MA /MABS  /K:.. /KEYS:..   UI, see README\n"
           "\n"
           "Exit codes: 0 ok, 1 a command failed, 2 no MSCDEX, 3 bad command line\n"
           "Example: CDPL /M:D P 3 W ST\n",
           ENGINE_BITS, plat_name());
}

static int bad_line(const char *what, const char *arg)
{
    printf("CDPL: %s: %s  (CDPL /? for help)\n", arg, what);
    return EXIT_USAGE;
}

/* "/NAME" or "/NAME:value" against a long and a short name; returns the
 * value ("" when none) or 0 if neither matches */
static const char *sw(const char *a, const char *lng, const char *shrt)
{
    const char *n[2];
    size_t l;
    int k;

    n[0] = lng;
    n[1] = shrt;
    for (k = 0; k < 2; k++) {
        l = strlen(n[k]);
        if (!strnicmp(a, n[k], l) && (a[l] == 0 || a[l] == ':'))
            return a[l] ? a + l + 1 : a + l;
    }
    return 0;
}

/* a switch that takes no value */
static int flag(const char *a, const char *lng, const char *shrt)
{
    const char *v = sw(a, lng, shrt);
    return v && !*v && a[strlen(a) - 1] != ':';
}

/* returns -1 to go on, else an exit code (help shown, or a bad line) */
static int parse_args(int argc, char **argv)
{
    int i, used, r;
    char err[80];
    const char *v;

    for (i = 1; i < argc; i += used) {
        char *a = argv[i];
        used = 1;
        if (a[0] != '/' && a[0] != '-') {
            r = cli_match(argc, argv, i, &used, err);
            if (r < 0)
                return bad_line(err, a);
            if (r == 0)
                return bad_line(isdigit((u8)a[0]) ? "argument without a command"
                                                  : "unknown command", a);
            opt.ncmds++;
            continue;
        }
        a++;
        if (!strcmp(a, "?") || !stricmp(a, "H")) { usage(); return EXIT_OK; }
        else if (!strcmp(a, "16") || !strcmp(a, "32")) {
            if (atoi(a) != ENGINE_BITS) {
                sprintf(err, "this is the %d-bit engine, use CDPL.EXE", ENGINE_BITS);
                return bad_line(err, argv[i]);
            }
        }
        else if ((v = sw(a, "MODE", "M")) != 0) {
            if (!stricmp(v, "A") || !stricmp(v, "ANALOG")) opt.mode = MODE_ANALOG;
            else if (!stricmp(v, "D") || !stricmp(v, "DIGITAL")) opt.mode = MODE_DIGITAL;
            else return bad_line("mode is /M:A (analog) or /M:D (digital)", argv[i]);
        }
        else if ((v = sw(a, "DRIVE", "D")) != 0) {
            if (!isalpha((u8)v[0]) || (v[1] && (v[1] != ':' || v[2])))
                return bad_line("drive is a letter, e.g. /D:G", argv[i]);
            opt.drive = toupper((u8)v[0]) - 'A';
        }
        else if (flag(a, "REDBOOK", "RB")) opt.redbook = 1;
        else if (flag(a, "NOFONT", "NF")) opt.nofont = 1;
        else if (flag(a, "BIOSFONT", "BF")) opt.biosfont = 1;
        else if (flag(a, "MABS", "MA")) opt.mabs = 1;
        else if ((v = sw(a, "KEYS", "K")) != 0 && *v)
            strncpy(opt.keys, v, sizeof(opt.keys) - 1);
        else if ((v = sw(a, "S", "S")) != 0 && *v)
            strncpy(opt.state, v, sizeof(opt.state) - 1);
        else if (!stricmp(a, "LAUNCHED")) opt.launched = 1;
        else
            return bad_line("unknown switch", argv[i]);
    }
    return -1;
}

static int drive_ok(void)
{
    int i;

    for (i = 0; i < cd.ndrives; i++)
        if (cd.letters[i] == opt.drive)
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    u32 start_tick;
    int r;

    r = parse_args(argc, argv);
    if (r >= 0)
        return r;
    plat_init();
    atexit(cleanup);
    signal(SIGINT, on_break);
    signal(SIGBREAK, on_break);
    sb_detect();
    pl_init();
    start_tick = bios_ticks();
    r = cd_init();
    if (opt.redbook)
        cd.redbook = 1;
    if (r >= 0 && opt.drive >= 0) {
        char b[40];
        if (!drive_ok()) {
            sprintf(b, "%c: is not a CD drive (first is %c:)", 'A' + opt.drive,
                    'A' + cd.letters[0]);
            return bad_line(b, "/D");
        }
        cd_select(opt.drive);
    }
    if (opt.ncmds) {
        if (opt.mode >= 0)
            pl.mode = opt.mode;
        return cli_run(argc, argv, r);
    }
    r = ui_run(r, start_tick);
    cleanup();
    return r;
}

