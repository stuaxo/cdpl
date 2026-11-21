/* launch.c - CDPL.EXE: runs CDPL16 or CDPL32 and switches between them
 * when one exits with code 16 or 32, so the two builds feel like one
 * program. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <dos.h>
#include <i86.h>

#define MAXARGS 16

/* INT 33h driver state, saved before the first engine and restored when
 * CDPL exits, so the shell gets its mouse cursor back as it was
 * whatever the engines did in between */
static char far *mstate;

static void mouse_save(void)
{
    union REGS r;
    struct SREGS sr;
    unsigned seg;

    /* no fn 0 here: a reset would hide the cursor we want to keep */
    if (_dos_getvect(0x33) == 0)
        return;
    r.x.ax = 0x15;                      /* state buffer size in BX */
    r.x.bx = 0;
    int86(0x33, &r, &r);
    if (r.x.bx == 0 || r.x.bx > 4096 ||
        _dos_allocmem((r.x.bx + 15) >> 4, &seg))
        return;
    mstate = MK_FP(seg, 0);
    segread(&sr);
    sr.es = seg;
    r.x.ax = 0x16;
    r.x.dx = 0;
    int86x(0x33, &r, &r, &sr);
}

static int mouse_restore(int rc)
{
    union REGS r;
    struct SREGS sr;

    if (mstate) {
        segread(&sr);
        sr.es = FP_SEG(mstate);
        r.x.ax = 0x17;
        r.x.dx = 0;
        int86x(0x33, &r, &r, &sr);
        _dos_freemem(FP_SEG(mstate));
        mstate = 0;
    }
    return rc;
}

static int is_engine_sw(const char *a)
{
    return (a[0] == '/' || a[0] == '-') && (!strcmp(a + 1, "16") || !strcmp(a + 1, "32"));
}

int main(int argc, char **argv)
{
    char dir[80], exe[96], sta[96], sarg[100], env[100];
    const char *args[MAXARGS + 4];
    char line[40];
    FILE *f;
    char *p;
    int engine = 16, i, n, rc, first = 1;

    strncpy(dir, argv[0], sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    p = strrchr(dir, '\\');
    if (!p)
        p = strrchr(dir, ':');
    if (p)
        p[1] = 0;
    else
        dir[0] = 0;
    sprintf(sta, "%sCDPL.STA", dir);

    /* DOS/4GW looks for itself on the PATH or in DOS4GPATH */
    sprintf(env, "DOS4GPATH=%s", dir[0] ? dir : ".");
    if (strlen(env) > 10 && env[strlen(env) - 1] == '\\')
        env[strlen(env) - 1] = 0;
    putenv(env);
    if (!getenv("DOS4G"))
        putenv("DOS4G=QUIET");

    f = fopen(sta, "r");
    if (f) {
        while (fgets(line, sizeof(line), f))
            if (!strncmp(line, "engine=", 7))
                engine = atoi(line + 7) == 32 ? 32 : 16;
        fclose(f);
    }
    for (i = 1; i < argc; i++)
        if (is_engine_sw(argv[i]))
            engine = atoi(argv[i] + 1);

    mouse_save();
    for (n = 0;; first = 0) {
        sprintf(exe, "%sCDPL%d.EXE", dir, engine);
        sprintf(sarg, "/S:%s", sta);
        n = 0;
        args[n++] = exe;
        args[n++] = "/LAUNCHED";
        args[n++] = sarg;
        for (i = 1; i < argc && n < MAXARGS; i++) {
            if (is_engine_sw(argv[i]))
                continue;
            /* scripted input is for the first engine only */
            if (!first && (argv[i][0] == '/' || argv[i][0] == '-') &&
                (!strnicmp(argv[i] + 1, "KEYS:", 5) || !strnicmp(argv[i] + 1, "K:", 2)))
                continue;
            args[n++] = argv[i];
        }
        args[n] = 0;
        rc = spawnv(P_WAIT, exe, args);
        if (rc == -1) {
            if (!first) {
                /* a switching engine left its 8-dot font mode up */
                union REGS r;
                r.x.ax = 0x0003;
                int86(0x10, &r, &r);
            }
            printf("CDPL: cannot run %s\n", exe);
            return mouse_restore(1);
        }
        if (rc == 16 || rc == 32) {
            engine = rc;
            continue;
        }
        return mouse_restore(rc);
    }
}
