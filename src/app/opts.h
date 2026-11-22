/* opts.h - switches from the command line, shared by main, ui and cli */
#ifndef OPTS_H
#define OPTS_H

typedef struct {
    int  launched;          /* /LAUNCHED: started by CDPL.EXE */
    int  nofont, biosfont, mabs;
    int  drive;             /* /D:x, 0=A; -1 none */
    int  mode;              /* /M:x, MODE_*; -1 none */
    int  redbook;
    int  ncmds;             /* command words on the line: no UI */
    char state[80];         /* /S:file */
    char keys[128];         /* /K:... */
} opts_t;

extern opts_t opt;

#endif
