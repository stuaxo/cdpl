/* state.c - key=value state file */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"

void state_defaults(appstate_t *s)
{
    memset(s, 0, sizeof(*s));
    s->engine = 16;
    s->drive = -1;
    s->vol = 255;
    s->mx = s->my = -1;
}

int state_load(const char *path, appstate_t *s)
{
    FILE *f = fopen(path, "r");
    char line[80], *eq;
    long v;

    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f)) {
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq++ = 0;
        v = strtol(eq, 0, 10);
        if (!strcmp(line, "engine")) s->engine = (int)v;
        else if (!strcmp(line, "mode")) s->mode = (int)v;
        else if (!strcmp(line, "drive")) s->drive = (int)v;
        else if (!strcmp(line, "state")) s->state = (int)v;
        else if (!strcmp(line, "pos")) s->pos = (u32)v;
        else if (!strcmp(line, "from")) s->from = (u32)v;
        else if (!strcmp(line, "to")) s->to = (u32)v;
        else if (!strcmp(line, "vol")) s->vol = (int)v;
        else if (!strcmp(line, "repeat")) s->repeat = (int)v;
        else if (!strcmp(line, "redbook")) s->redbook = (int)v;
        else if (!strcmp(line, "cdmixer")) s->cd_mixer = (int)v;
        else if (!strcmp(line, "sel")) s->sel = (int)v;
        else if (!strcmp(line, "focus")) s->focus = (int)v;
        else if (!strcmp(line, "mx")) s->mx = (int)v;
        else if (!strcmp(line, "my")) s->my = (int)v;
        else if (!strcmp(line, "switched")) s->switched = (int)v;
    }
    fclose(f);
    return 0;
}

int state_save(const char *path, const appstate_t *s)
{
    FILE *f = fopen(path, "w");

    if (!f)
        return -1;
    fprintf(f, "engine=%d\nmode=%d\ndrive=%d\nstate=%d\n", s->engine,
            s->mode, s->drive, s->state);
    fprintf(f, "pos=%lu\nfrom=%lu\nto=%lu\n", s->pos, s->from, s->to);
    fprintf(f, "vol=%d\nrepeat=%d\nredbook=%d\ncdmixer=%d\n", s->vol,
            s->repeat, s->redbook, s->cd_mixer);
    fprintf(f, "sel=%d\nfocus=%d\nswitched=%d\n", s->sel, s->focus,
            s->switched);
    fprintf(f, "mx=%d\nmy=%d\n", s->mx, s->my);
    fclose(f);
    return 0;
}
