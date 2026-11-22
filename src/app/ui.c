/* ui.c - the full-screen player: main screen, menus, dialogs, mouse and
 * keyboard handling.  Uses the core (CD, sound, player) and the TUI
 * toolkit; start-up and the command line are in main.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "plat.h"
#include "mscdex.h"
#include "sb.h"
#include "player.h"
#include "tui.h"
#include "vga.h"
#include "mouse.h"
#include "glyphs.h"
#include "state.h"
#include "info.h"
#include "opts.h"
#include "cpu.h"
#include "ui.h"

/* ---- globals ---------------------------------------------------------- */

static volatile int g_quit;
static int  g_exitcode;
static int  repeat_on;
static int  sel_track;              /* list selection (track number) */
static int  list_top;
static int  focus;
static int  pressed = -1;           /* control held down by the mouse */
static int  dragging = -1;
static u32  start_tick;
static int  ui_up;
static int  can_switch;             /* CDPL.EXE and a 386: show the switch */

static void cmd_exec(int cmd);

/* ---- controls --------------------------------------------------------- */

enum { CT_LIST, CT_ICON, CT_RADIO, CT_CHECK, CT_SLIDER, CT_SEEK };
enum {
    ID_LIST, ID_SEEK, ID_PREV, ID_REW, ID_PLAY, ID_PAUSE, ID_STOP, ID_FFWD,
    ID_NEXT, ID_EJECT, ID_ANALOG, ID_DIGITAL, ID_E16, ID_E32, ID_REPEAT,
    ID_REDBOOK, ID_VOL, ID_N
};

typedef struct {
    int type, x, y, w, h;
    const char *label;
    u16 altkey;                 /* Alt+key scan code that focuses/activates */
    int icon;
} ctl_t;

#define LIST_X 3
#define LIST_Y 4
#define LIST_W 31
#define LIST_H 11
#define PANEL_X 38

static ctl_t ctl[ID_N] = {
    { CT_LIST,   LIST_X, LIST_Y, LIST_W + 1, LIST_H, "~T~racks", 0x14, 0 },
    { CT_SEEK,   39, 7, 36, 1, 0, 0, 0 },
    { CT_ICON,   39, 9, 4, 1, 0, 0, IC_PREV },
    { CT_ICON,   44, 9, 4, 1, 0, 0, IC_REW },
    { CT_ICON,   49, 9, 4, 1, 0, 0, IC_PLAY },
    { CT_ICON,   54, 9, 4, 1, 0, 0, IC_PAUSE },
    { CT_ICON,   59, 9, 4, 1, 0, 0, IC_STOP },
    { CT_ICON,   64, 9, 4, 1, 0, 0, IC_FFWD },
    { CT_ICON,   69, 9, 4, 1, 0, 0, IC_NEXT },
    { CT_ICON,   70, 11, 4, 1, 0, 0, IC_EJECT },
    { CT_RADIO,  46, 11, 9, 1, "~A~nalog", 0x1E, 0 },
    { CT_RADIO,  57, 11, 10, 1, "D~i~gital", 0x17, 0 },
    { CT_RADIO,  46, 12, 9, 1, "~1~6-bit", 0x78, 0 },
    { CT_RADIO,  57, 12, 10, 1, "~3~2-bit", 0x7A, 0 },
    { CT_CHECK,  39, 13, 10, 1, "~R~epeat", 0x13, 0 },
    { CT_CHECK,  52, 13, 22, 1, "Red Boo~k~ addressing", 0x25, 0 },
    { CT_SLIDER, 47, 14, 20, 1, "~V~olume", 0x2F, 0 },
};

static int inside(const ctl_t *c, int cx, int cy)
{
    int w = c->w;
    if (c->type == CT_LIST)
        w = LIST_W + 1;
    return cx >= c->x && cx < c->x + w && cy >= c->y && cy < c->y + c->h;
}

/* ---- helpers ----------------------------------------------------------- */

static int first_audio(void)
{
    int t;
    for (t = cd.toc.first; t <= cd.toc.last; t++)
        if (track_is_audio(t))
            return t;
    return 0;
}

static void play_track(int t)
{
    if (!cd.toc.valid || t < cd.toc.first || t > cd.toc.last)
        return;
    if (!track_is_audio(t)) {
        sprintf(pl.err, "Track %d is a data track", t);
        return;
    }
    sel_track = t;
    pl_play(cd.toc.t[t].start, pl_audio_end(cd.toc.t[t].start));
}

static void step_track(int dir)
{
    int t = pl.state != PS_STOPPED ? pl.track : sel_track;

    if (!t)
        t = first_audio();
    for (t += dir; t >= cd.toc.first && t <= cd.toc.last; t += dir)
        if (track_is_audio(t)) {
            if (pl.state == PS_STOPPED)
                sel_track = t;
            else
                play_track(t);
            return;
        }
}

static void seek_rel(long frames)
{
    long np;

    if (pl.state == PS_STOPPED)
        return;
    np = (long)pl.pos + frames;
    if (np < (long)pl.from)
        np = (long)pl.from;
    if (np >= (long)pl.to)
        np = (long)pl.to - 75;
    pl_play((u32)np, pl.to);
}

static void toggle_pause(void)
{
    if (pl.state == PS_PLAYING)
        pl_pause();
    else if (pl.state == PS_PAUSED)
        pl_resume();
    else
        play_track(sel_track ? sel_track : first_audio());
}

static void idle(void)
{
    rmregs_t r;

    pl_poll();
    if (pl.ended) {
        pl.ended = 0;
        if (repeat_on)
            play_track(first_audio());
    }
    /* give the time slice back to a multitasker / emulator */
    rm_clear(&r);
    r.eax = 0x1680;
    rm_int(0x2F, &r);
}

static void msf_str(char *b, u32 frames, int with_frames)
{
    int m, s, f;
    frame_msf(frames, &m, &s, &f);
    if (with_frames)
        sprintf(b, "%02d:%02d.%02d", m, s, f);
    else
        sprintf(b, "%02d:%02d", m, s);
}

/* ---- drawing the main screen ------------------------------------------- */

static void draw_menubar(void);
static void draw_status(void);

static void draw_list(void)
{
    const ctl_t *c = &ctl[ID_LIST];
    int i, n = 0, foc = focus == ID_LIST;
    int total = cd.toc.valid ? cd.toc.last - cd.toc.first + 1 : 0;
    int selrow = sel_track - cd.toc.first;

    tui_hstr(c->x, c->y - 1, " Trk Type   Start     Len  Flg", A_LIST_HDR, A_LIST_HDR);
    tui_fill(c->x, c->y - 1, LIST_W, 1, 0, A_LIST_HDR);
    tui_hstr(c->x, c->y - 1, " Trk Type   Start     Len  Flg", A_LIST_HDR, A_LIST_HDR);
    if (selrow < list_top)
        list_top = selrow;
    if (selrow >= list_top + LIST_H)
        list_top = selrow - LIST_H + 1;
    if (list_top < 0)
        list_top = 0;
    for (i = 0; i < LIST_H; i++) {
        int t = cd.toc.first + list_top + i;
        char line[48], st[12], ln[8];
        u8 a = A_LIST;
        if (!cd.toc.valid || t > cd.toc.last) {
            tui_fill(c->x, c->y + i, LIST_W, 1, ' ', A_LIST);
            continue;
        }
        msf_str(st, cd.toc.t[t].start, 1);
        msf_str(ln, cd.toc.t[t].len, 0);
        sprintf(line, "  %2d %-5s %s  %s  %c%c%c   ", t,
                track_is_audio(t) ? "Audio" : "Data", st, ln,
                (cd.toc.t[t].ctrl & 0x80) ? '4' : ' ',
                (cd.toc.t[t].ctrl & 0x10) ? 'P' : ' ',
                (cd.toc.t[t].ctrl & 0x20) ? 'C' : ' ');
        line[LIST_W] = 0;
        if (t == sel_track)
            a = foc ? A_LIST_SELF : A_LIST_SEL;
        tui_str(c->x, c->y + i, line, a);
        if (pl.state != PS_STOPPED && t == pl.track)
            tui_put(c->x, c->y + i, tui_glyphs ? G_NOTE : 0x0E, a);
        else if (!track_is_audio(t))
            tui_put(c->x, c->y + i, tui_glyphs ? G_DISC : 0x09, a);
        n++;
    }
    /* scrollbar */
    {
        int sx = c->x + LIST_W, th;
        tui_put(sx, c->y, 0x1E, 0x30);
        tui_put(sx, c->y + LIST_H - 1, 0x1F, 0x30);
        for (i = 1; i < LIST_H - 1; i++)
            tui_put(sx, c->y + i, 0xB1, 0x13);
        if (total > LIST_H) {
            th = 1 + (selrow * (LIST_H - 3)) / (total - 1);
        } else
            th = 1;
        tui_put(sx, c->y + th, 0xDB, 0x13);
    }
    (void)n;
}

static void btn_shadow(int x, int y, int w)
{
    int i;
    u8 sa = (u8)(A_WIN & 0xF0);
    tui_put(x + w, y, 0xDC, sa);
    for (i = 1; i <= w; i++)
        tui_put(x + i, y + 1, 0xDF, sa);
}

static void draw_icon_btn(int id)
{
    const ctl_t *c = &ctl[id];
    int down = pressed == id;
    int active = (id == ID_PLAY && pl.state == PS_PLAYING) ||
                 (id == ID_PAUSE && pl.state == PS_PAUSED) ||
                 (id == ID_STOP && pl.state == PS_STOPPED);
    u8 a = focus == id ? A_BTN_FOC : active ? 0x2F : A_BTN;
    int x = c->x + down;

    tui_fill(c->x, c->y, c->w + 1, 2, ' ', A_WIN);
    if (!down)
        btn_shadow(c->x, c->y, c->w);
    tui_put(x, c->y, ' ', a);
    tui_icon(x + 1, c->y, c->icon, a);
    tui_put(x + 3, c->y, ' ', a);
}

static void draw_toggle(int id, int on)
{
    const ctl_t *c = &ctl[id];
    u8 a = focus == id ? A_FOCUS : A_LABEL;
    int x = c->x;

    if (tui_glyphs) {
        tui_put(x, c->y, c->type == CT_RADIO ? g_radio(on) : g_check(on), a);
        x += 1;
    } else {
        tui_put(x, c->y, c->type == CT_RADIO ? '(' : '[', a);
        tui_put(x + 1, c->y, c->type == CT_RADIO ? g_radio(on) : g_check(on), a);
        tui_put(x + 2, c->y, c->type == CT_RADIO ? ')' : ']', a);
        x += 3;
    }
    tui_hstr(x, c->y, c->label, a, focus == id ? 0x7E : A_LABEL_HOT);
}

static void draw_lcd(void)
{
    char b[64], t1[12], t2[12], t3[12], t4[12];
    int x = PANEL_X, y = 3;
    u32 tstart = 0, tlen = 0;
    int trk = pl.state != PS_STOPPED ? pl.track : sel_track;
    static const char *sname[3] = { "STOPPED", "PLAYING", "PAUSED " };

    tui_fill(x, y, 38, 3, ' ', A_LCD);
    if (trk && cd.toc.valid) {
        tstart = cd.toc.t[trk].start;
        tlen = cd.toc.t[trk].len;
    }
    sprintf(b, " Track %02d/%02d  Index %02d", trk, cd.toc.valid ? cd.toc.last : 0,
            pl.state != PS_STOPPED && pl.mode == MODE_ANALOG ? pl.index : 0);
    tui_str(x, y, b, A_LCD);
    tui_str(x + 30, y, sname[pl.state], pl.state == PS_PLAYING ? A_LCD_HI : A_LCD);
    msf_str(t1, pl.state != PS_STOPPED && pl.pos > tstart ? pl.pos - tstart : 0, 0);
    msf_str(t2, tlen, 0);
    msf_str(t3, pl.state != PS_STOPPED && pl.pos > 150 ? pl.pos - 150 : 0, 0);
    msf_str(t4, cd.toc.leadout > 150 ? cd.toc.leadout - 150 : 0, 0);
    sprintf(b, " %s / %s      Disc %s / %s", t1, t2, t3, t4);
    tui_str(x, y + 1, b, A_LCD_HI);
    if (pl.err[0]) {
        sprintf(b, " %-37.37s", pl.err);
        tui_str(x, y + 2, b, 0x0C);
    } else if (pl.mode == MODE_DIGITAL) {
        sprintf(b, " DIGITAL blk %-5lu und %-3lu err %-3lu", pl.blocks,
                pl.underruns, pl.read_errors);
        tui_str(x, y + 2, b, A_LCD);
        /* small level meter */
        tui_bar(x + 34, y + 2, 2, pl.peak_l + pl.peak_r, 65534UL, A_LCD);
    } else {
        sprintf(b, " ANALOG  st=%04X %s%s", pl.status,
                (pl.status & ST_BUSY) ? "BUSY " : "     ",
                (pl.status & ST_ERROR) ? "ERROR" : "");
        tui_str(x, y + 2, b, A_LCD);
    }
    /* seek bar: position within the track */
    {
        const ctl_t *c = &ctl[ID_SEEK];
        u32 v = pl.state != PS_STOPPED && pl.pos > tstart ? pl.pos - tstart : 0;
        tui_put(c->x - 1, c->y, '[', A_WIN);
        tui_bar(c->x, c->y, c->w, v, tlen, focus == ID_SEEK ? A_BAR_FOC : A_BAR);
        tui_put(c->x + c->w, c->y, ']', A_WIN);
    }
}

static void draw_log(void)
{
    int i;
    char b[96];

    tui_window(1, 18, 77, 6, " MSCDEX request log ", A_LOG, 0x1F);
    for (i = 0; i < 4; i++) {
        const cdlog_t *l = cd_log_get((unsigned)(3 - i));
        if (!l)
            continue;
        cd_log_format(l, start_tick, b);
        b[74] = 0;
        tui_str(3, 19 + i, b, (l->status & ST_ERROR) || l->cf ? A_LOG_ERR : A_LOG);
    }
}

static void draw_main(void)
{
    char title[80];
    int i;

    tui_fill(0, 1, SCR_W, SCR_H - 2, 0xB0, A_DESK);
    draw_menubar();
    sprintf(title, " CD Player  %c:  MSCDEX %u.%02u  %s ", 'A' + cd.cur,
            cd.version >> 8, cd.version & 0xFF, cd.devname);
    tui_window(1, 2, 77, 15, title, A_WIN, A_WIN_FRAME);
    tui_shadow(1, 2, 77, 15);
    /* divider between list and panel */
    for (i = 3; i < 16; i++)
        tui_put(36, i, BX_V, A_WIN_FRAME);
    tui_put(36, 16, 0xCF, A_WIN_FRAME);

    draw_list();
    draw_lcd();
    for (i = ID_PREV; i <= ID_NEXT; i++)
        draw_icon_btn(i);
    tui_str(39, 11, "Mode", A_LABEL);
    tui_str(39, 12, "Engine", A_LABEL);
    draw_icon_btn(ID_EJECT);
    draw_toggle(ID_ANALOG, pl.mode == MODE_ANALOG);
    draw_toggle(ID_DIGITAL, pl.mode == MODE_DIGITAL);
    if (can_switch) {
        draw_toggle(ID_E16, ENGINE_BITS == 16);
        draw_toggle(ID_E32, ENGINE_BITS == 32);
    } else {
        tui_str(46, 12, plat_name(), A_LABEL);  /* just say which one */
    }
    draw_toggle(ID_REPEAT, repeat_on);
    draw_toggle(ID_REDBOOK, cd.redbook);
    {
        const ctl_t *c = &ctl[ID_VOL];
        char b[8];
        tui_hstr(39, c->y, c->label, focus == ID_VOL ? A_FOCUS : A_LABEL,
                 focus == ID_VOL ? 0x7E : A_LABEL_HOT);
        tui_bar(c->x, c->y, c->w, pl.vol, 255, focus == ID_VOL ? A_BAR_FOC : A_BAR);
        sprintf(b, "%3d%%", pl.vol * 100 / 255);
        tui_str(c->x + c->w + 1, c->y, b, A_LABEL);
    }
    draw_log();
    draw_status();
}

static void draw_status(void)
{
    tui_fill(0, 24, SCR_W, 1, ' ', A_STATUS);
    tui_hstr(0, 24, " ~F1~ Help  ~F10~ Menu  ~P~ Play  ~Space~ Pause  ~S~ Stop"
             "  ~N~/~B~ Track  ~M~ Mode  ~Alt-X~ Exit", A_STATUS, A_STATUS_K);
}

/* ---- menus ----------------------------------------------------------- */

enum {
    C_NONE, C_INFO, C_PEEK, C_SOUND, C_LOG, C_EXIT, C_DRIVE_NEXT, C_EJECT,
    C_CLOSE, C_LOCK, C_UNLOCK, C_RESET, C_TOC, C_PLAY, C_PAUSE, C_STOP,
    C_NEXT, C_PREV, C_ANALOG, C_DIGITAL, C_E16, C_E32, C_REPEAT, C_REDBOOK,
    C_CDMIX, C_HELP, C_ABOUT
};

typedef struct { const char *label, *key; int cmd; } mitem_t;
typedef struct { const char *title; int x; const mitem_t *items; int n; } menu_t;

static const mitem_t m_file[] = {
    { "Drive ~I~nfo...", "I", C_INFO },
    { "~R~aw Sector Peek...", "R", C_PEEK },
    { "~S~ound Card...", "", C_SOUND },
    { "Request ~L~og...", "L", C_LOG },
    { 0, 0, 0 },
    { "E~x~it", "Alt-X", C_EXIT } };
static const mitem_t m_drive[] = {
    { "~N~ext Drive", "", C_DRIVE_NEXT },
    { "~E~ject", "E", C_EJECT },
    { "~C~lose Tray", "C", C_CLOSE },
    { "~L~ock Door", "", C_LOCK },
    { "~U~nlock Door", "", C_UNLOCK },
    { "Rese~t~ Drive", "", C_RESET },
    { "Re-read ~T~OC", "", C_TOC } };
static const mitem_t m_play[] = {
    { "~P~lay", "P", C_PLAY },
    { "P~a~use/Resume", "Space", C_PAUSE },
    { "~S~top", "S", C_STOP },
    { 0, 0, 0 },
    { "~N~ext Track", "N", C_NEXT },
    { "Pre~v~ious Track", "B", C_PREV } };
static const mitem_t m_opt[] = {
    { "~A~nalog (PLAY AUDIO)", "M", C_ANALOG },
    { "~D~igital (raw READ LONG)", "M", C_DIGITAL },
    { 0, 0, 0 },
    { "~R~epeat", "", C_REPEAT },
    { "Red ~B~ook addressing", "", C_REDBOOK },
    { "SB ~C~D mixer volume", "", C_CDMIX },
    /* engine items last: cut off when not started by CDPL.EXE */
    { 0, 0, 0 },
    { "~1~6-bit real mode engine", "", C_E16 },
    { "~3~2-bit DPMI engine", "", C_E32 } };
#define M_OPT_SOLO 6                /* items without the engine group */
static const mitem_t m_help[] = {
    { "~K~eys...", "F1", C_HELP },
    { "~A~bout...", "", C_ABOUT } };

#define NMENU 5
static menu_t menus[NMENU] = {
    { "~F~ile", 1, m_file, 6 },
    { "~D~rive", 7, m_drive, 7 },
    { "~P~lay", 14, m_play, 6 },
    { "~O~ptions", 20, m_opt, 9 },
    { "~H~elp", 29, m_help, 2 } };
static const char menu_alt[NMENU] = { 'f', 'd', 'p', 'o', 'h' };

static int menu_mark(int cmd)       /* -1 none, else 0/1 for a check */
{
    switch (cmd) {
    case C_ANALOG:  return pl.mode == MODE_ANALOG;
    case C_DIGITAL: return pl.mode == MODE_DIGITAL;
    case C_E16:     return ENGINE_BITS == 16;
    case C_E32:     return ENGINE_BITS == 32;
    case C_REPEAT:  return repeat_on;
    case C_REDBOOK: return cd.redbook;
    case C_CDMIX:   return pl.cd_mixer;
    }
    return -1;
}

static void draw_menubar_sel(int open)
{
    int i;
    char eng[40];

    tui_fill(0, 0, SCR_W, 1, ' ', A_MENU);
    for (i = 0; i < NMENU; i++) {
        int sel = i == open;
        tui_put(menus[i].x, 0, ' ', sel ? A_MENU_SEL : A_MENU);
        tui_hstr(menus[i].x + 1, 0, menus[i].title, sel ? A_MENU_SEL : A_MENU,
                 sel ? A_MENU_SELH : A_MENU_HOT);
        tui_put(menus[i].x + 1 + tui_hlen(menus[i].title), 0, ' ',
                sel ? A_MENU_SEL : A_MENU);
    }
    sprintf(eng, "CDPL %s ", plat_name());
    tui_str(SCR_W - (int)strlen(eng), 0, eng, 0x71);
}

static void draw_menubar(void) { draw_menubar_sel(-1); }

static int menu_width(const menu_t *m)
{
    int i, w = 0;
    for (i = 0; i < m->n; i++)
        if (m->items[i].label) {
            int l = tui_hlen(m->items[i].label) + (int)strlen(m->items[i].key) + 6;
            if (l > w)
                w = l;
        }
    return w;
}

static void draw_dropdown(int mi, int sel)
{
    const menu_t *m = &menus[mi];
    int w = menu_width(m), i, x = m->x, y = 1;

    tui_window(x, y, w, m->n + 2, 0, A_MENU, A_MENU);
    tui_shadow(x, y, w, m->n + 2);
    /* the window helper drew a close box; menus have a plain frame */
    tui_box(x, y, w, m->n + 2, A_MENU, 0);
    for (i = 0; i < m->n; i++) {
        const mitem_t *it = &m->items[i];
        int yy = y + 1 + i, mk;
        u8 a = i == sel ? A_MENU_SEL : A_MENU;
        if (!it->label) {
            int j;
            tui_put(x, yy, 0xC3, A_MENU);
            for (j = 1; j < w - 1; j++)
                tui_put(x + j, yy, BX_H, A_MENU);
            tui_put(x + w - 1, yy, 0xB4, A_MENU);
            continue;
        }
        tui_fill(x + 1, yy, w - 2, 1, ' ', a);
        mk = menu_mark(it->cmd);
        if (mk >= 0) {
            int radio = it->cmd == C_ANALOG || it->cmd == C_DIGITAL ||
                        it->cmd == C_E16 || it->cmd == C_E32;
            tui_put(x + 1, yy, radio ? g_radio(mk) : (u8)(mk ? (tui_glyphs ? G_CHK_ON : 0xFB) : ' '), a);
        }
        tui_hstr(x + 3, yy, it->label, a, i == sel ? A_MENU_SELH : A_MENU_HOT);
        tui_str(x + w - 2 - (int)strlen(it->key), yy, it->key, a);
    }
}

static int next_item(const menu_t *m, int i, int dir)
{
    int k;
    for (k = 0; k < m->n; k++) {
        i = (i + dir + m->n) % m->n;
        if (m->items[i].label)
            return i;
    }
    return i;
}

static int item_hot(const mitem_t *it)
{
    const char *p = strchr(it->label, '~');
    return p ? tolower((u8)p[1]) : 0;
}

/* modal menu; returns a command or 0 */
static int menu_run(int mi)
{
    int sel = 0, i;
    event_t e;

    for (;;) {
        const menu_t *m = &menus[mi];
        int w = menu_width(m);
        if (!m->items[sel].label)
            sel = next_item(m, sel, 1);
        draw_main();
        draw_menubar_sel(mi);
        draw_dropdown(mi, sel);
        tui_flush();
        idle();
        if (g_quit)
            return 0;
        while (tui_event(&e)) {
            if (e.type == EV_KEY) {
                int c = key_ascii(e.key), a = alt_letter(e.key);
                if (e.key == K_ESC || e.key == K_F10)
                    return 0;
                if (e.key == K_UP)    sel = next_item(m, sel, -1);
                if (e.key == K_DOWN)  sel = next_item(m, sel, 1);
                if (e.key == K_LEFT)  { mi = (mi + NMENU - 1) % NMENU; sel = 0; }
                if (e.key == K_RIGHT) { mi = (mi + 1) % NMENU; sel = 0; }
                if (e.key == K_ENTER) return m->items[sel].cmd;
                if (a) {
                    for (i = 0; i < NMENU; i++)
                        if (menu_alt[i] == a) { mi = i; sel = 0; }
                    if (a == 'x') return C_EXIT;
                }
                if (c)
                    for (i = 0; i < m->n; i++)
                        if (m->items[i].label && item_hot(&m->items[i]) == c)
                            return m->items[i].cmd;
                break;
            } else if (e.type == EV_MDOWN || e.type == EV_MUP || e.type == EV_MMOVE) {
                int in = e.cx > m->x && e.cx < m->x + w - 1 &&
                         e.cy >= 2 && e.cy < 2 + m->n;
                if (in && m->items[e.cy - 2].label)
                    sel = e.cy - 2;
                if (e.cy == 0)
                    for (i = 0; i < NMENU; i++)
                        if (e.cx >= menus[i].x &&
                            e.cx < menus[i].x + tui_hlen(menus[i].title) + 2 &&
                            (e.held || e.type == EV_MDOWN) && i != mi) {
                            mi = i;
                            sel = 0;
                        }
                if (e.type == EV_MUP && in && m->items[e.cy - 2].label)
                    return m->items[sel].cmd;
                if (e.type == EV_MDOWN && !in && e.cy != 0)
                    return 0;
            }
        }
    }
}

/* ---- dialogs ----------------------------------------------------------- */

#define DLG_MAX 18

/* simple modal text box with an OK button */
static void msgbox(const char *title, char lines[][72], int n)
{
    int w = 0, i, h, x, y, bx, by, down = 0;
    event_t e;

    for (i = 0; i < n; i++)
        if ((int)strlen(lines[i]) > w)
            w = (int)strlen(lines[i]);
    if ((int)strlen(title) + 6 > w)
        w = (int)strlen(title) + 6;
    w += 6;
    if (w > 76) w = 76;
    h = n + 6;
    x = (SCR_W - w) / 2;
    y = (SCR_H - h) / 2;
    bx = x + (w - 8) / 2;
    by = y + h - 3;
    for (;;) {
        draw_main();
        tui_window(x, y, w, h, title, A_DLG_TEXT, A_DLG);
        tui_shadow(x, y, w, h);
        for (i = 0; i < n; i++)
            tui_str(x + 3, y + 2 + i, lines[i], A_DLG_TEXT);
        tui_fill(bx, by, 9, 2, ' ', A_DLG_TEXT);
        if (!down) {
            int j;
            tui_put(bx + 8, by, 0xDC, A_DLG_TEXT & 0xF0);
            for (j = 1; j <= 8; j++)
                tui_put(bx + j, by + 1, 0xDF, A_DLG_TEXT & 0xF0);
        }
        tui_hstr(bx + down, by, "   ~O~K   ", 0x2F, 0x2E);
        tui_flush();
        idle();
        if (g_quit)
            return;
        while (tui_event(&e)) {
            if (e.type == EV_KEY && (e.key == K_ESC || e.key == K_ENTER ||
                                     key_ascii(e.key) == 'o' || e.key == K_SPACE))
                return;
            if (e.type == EV_MDOWN && e.cy == by && e.cx >= bx && e.cx < bx + 8)
                down = 1;
            if (e.type == EV_MUP) {
                if (down && e.cy == by && e.cx >= bx && e.cx < bx + 8)
                    return;
                down = 0;
            }
            if (e.type == EV_MDOWN && e.cy == y && e.cx >= x + 1 && e.cx <= x + 3)
                return;                 /* close box */
        }
    }
}

static void dlg_info(void)
{
    static char l[INFO_MAX][INFO_W];
    int n = info_lines(l, INFO_MAX);

    msgbox(" Drive Information ", l, n);
}

static void dlg_sound(void)
{
    static char l[8][72];
    int n = 0;
    const char *b = getenv("BLASTER");

    sprintf(l[n++], "BLASTER  %s", b ? b : "(not set)");
    sprintf(l[n++], "Card     %s", sb.desc);
    if (sb.present)
        sprintf(l[n++], "Port %Xh  IRQ %d  DMA %d  HDMA %d", sb.base, sb.irq,
                sb.dma8, sb.dma16);
    sprintf(l[n++], "Digital  %lu blocks, %lu underruns, %lu read errors",
            pl.blocks, pl.underruns, pl.read_errors);
    sprintf(l[n++], "IRQs     %u", sb_irq_count);
    msgbox(" Sound Card ", l, n);
}

static void dlg_help(void)
{
    static char l[16][72] = {
        "P / Enter      play selected track",
        "Space          pause / resume",
        "S              stop",
        "N / B          next / previous track",
        "Left / Right   seek -/+ 5 s (on the list or seek bar)",
        "1-9            play track 1-9",
        "M              toggle Analog / Digital",
        "E / C          eject / close tray",
        "I / R / L      drive info / raw sector peek / request log",
        "Tab            move between controls",
        "Alt+letter     menus and controls",
        "F10            menu bar,   Alt-X  exit",
        "",
        "Mouse: click buttons, double-click a track, drag the bars." };
    msgbox(" Keys ", l, 14);
}

static void dlg_about(void)
{
    static char l[8][72];
    int n = 0;

    sprintf(l[n++], "CDPL 1.0 - CD audio test player for DOS and emulators");
    sprintf(l[n++], "");
    sprintf(l[n++], "This engine: %s (%d-bit build)", plat_name(), ENGINE_BITS);
    sprintf(l[n++], "Analog:  MSCDEX PLAY AUDIO, drive plays the audio");
    sprintf(l[n++], "Digital: READ LONG raw 2352-byte sectors to a Sound Blaster");
    sprintf(l[n++], "Font: %s", tui_glyphs ? (vga_biosfont ? "custom (INT 10h)" : "custom (plane 2)") : "standard");
    msgbox(" About CDPL ", l, n);
}

static void dlg_log(void)
{
    int top = 0, i, x = 2, y = 2, w = 76, h = 20;
    event_t e;
    char b[96];
    unsigned total = cd.nlog < CDLOG_N ? cd.nlog : CDLOG_N;

    for (;;) {
        draw_main();
        tui_window(x, y, w, h, " Request Log (newest first) ", A_LOG, 0x1F);
        tui_shadow(x, y, w, h);
        for (i = 0; i < h - 2; i++) {
            const cdlog_t *l = cd_log_get((unsigned)(top + i));
            if (!l)
                break;
            cd_log_format(l, start_tick, b);
            b[w - 4] = 0;
            tui_str(x + 2, y + 1 + i, b, (l->status & ST_ERROR) || l->cf ? A_LOG_ERR : A_LOG);
        }
        tui_flush();
        idle();
        if (g_quit)
            return;
        while (tui_event(&e)) {
            if (e.type == EV_KEY) {
                if (e.key == K_ESC || e.key == K_ENTER) return;
                if (e.key == K_DOWN && top + h - 2 < (int)total) top++;
                if (e.key == K_UP && top > 0) top--;
                if (e.key == K_PGDN) top += h - 2;
                if (e.key == K_PGUP) top -= h - 2;
                if (top > (int)total - (h - 2)) top = (int)total - (h - 2);
                if (top < 0) top = 0;
            }
            if (e.type == EV_MDOWN && (e.button == 2 ||
                (e.cy == y && e.cx >= x + 1 && e.cx <= x + 3)))
                return;
        }
    }
}

static void dlg_peek(void)
{
    static dosbuf_t sec;
    int x = 4, y = 3, w = 72, h = 19, i, j, mode = 1;
    u32 frame;
    u16 st = 0;
    s32 pk_l = 0, pk_r = 0;
    int need = 1;
    event_t e;
    char b[96];

    if (!sec.seg && dos_alloc(&sec, 2352))
        return;
    frame = pl.state != PS_STOPPED ? pl.pos :
            (sel_track ? cd.toc.t[sel_track].start : 150);
    for (;;) {
        if (need) {
            fmemset(sec.p, 0, 2352);
            st = cd_read_long(frame, 1, sec.seg, 0, (u8)mode);
            cd_log(CMD_READ_LONG, (u8)mode, st, 0, "peek %s %lu", mode ? "raw" : "cooked", frame - 150);
            pk_l = pk_r = 0;
            for (i = 0; i < 588; i++) {
                s16 FAR *s = (s16 FAR *)sec.p;
                s32 a = s[i * 2], c = s[i * 2 + 1];
                if (a < 0) a = -a;
                if (c < 0) c = -c;
                if (a > pk_l) pk_l = a;
                if (c > pk_r) pk_r = c;
            }
            need = 0;
        }
        draw_main();
        tui_window(x, y, w, h, " Raw Sector Peek ", A_DLG_TEXT, A_DLG);
        tui_shadow(x, y, w, h);
        {
            char ms[12];
            int t = cd_track_of(frame);
            msf_str(ms, frame, 1);
            sprintf(b, "Sector %lu (%s) track %d %s   %s read, status %04X %s",
                    frame - 150, ms, t, track_is_audio(t) ? "audio" : "data",
                    mode ? "raw" : "cooked", st, (st & ST_ERROR) ? "ERROR" : "ok");
            tui_str(x + 2, y + 1, b, A_DLG_TEXT);
        }
        for (j = 0; j < 12; j++) {
            u8 FAR *p = sec.p + j * 16;
            int k, o = 0;
            o += sprintf(b + o, "%04X ", j * 16);
            for (k = 0; k < 16; k++)
                o += sprintf(b + o, "%02X%s", p[k], k == 7 ? "  " : " ");
            for (k = 0; k < 16; k++)
                b[o++] = (char)(p[k] >= 32 && p[k] < 127 ? p[k] : '.');
            b[o] = 0;
            tui_str(x + 2, y + 3 + j, b, A_DLG_TEXT);
        }
        sprintf(b, "Peak  L %5ld  R %5ld", pk_l, pk_r);
        tui_str(x + 2, y + 15, b, A_DLG_TEXT);
        tui_bar(x + 26, y + 15, 20, (u32)pk_l, 32767UL, 0x3E);
        tui_bar(x + 47, y + 15, 20, (u32)pk_r, 32767UL, 0x3E);
        tui_hstr(x + 2, y + 17, "~PgUp/PgDn~ sector  ~Up/Dn~ 1 s  ~Home~ track start  "
                 "~C~ooked/raw  ~Esc~", A_DLG_TEXT, 0x3E);
        tui_flush();
        idle();
        if (g_quit)
            return;
        while (tui_event(&e)) {
            if (e.type == EV_KEY) {
                int c = key_ascii(e.key);
                if (e.key == K_ESC || e.key == K_ENTER) return;
                if (e.key == K_PGDN) { frame++; need = 1; }
                if (e.key == K_PGUP && frame > 150) { frame--; need = 1; }
                if (e.key == K_DOWN) { frame += 75; need = 1; }
                if (e.key == K_UP && frame >= 225) { frame -= 75; need = 1; }
                if (e.key == K_HOME) {
                    int t = cd_track_of(frame);
                    if (t) frame = cd.toc.t[t].start;
                    need = 1;
                }
                if (c == 'c') { mode = !mode; need = 1; }
            }
            if (e.type == EV_MDOWN && (e.button == 2 ||
                (e.cy == y && e.cx >= x + 1 && e.cx <= x + 3)))
                return;
        }
    }
}

static void error_box(const char *title, const char *msg)
{
    static char l[2][72];
    strncpy(l[0], msg, 70);
    l[0][70] = 0;
    msgbox(title, l, 1);
}

/* ---- engine switch / state ------------------------------------------- */

static void fill_state(appstate_t *s)
{
    state_defaults(s);
    s->engine = ENGINE_BITS;
    s->mode = pl.mode;
    s->drive = cd.cur;
    s->state = pl.state;
    s->pos = pl.pos;
    s->from = pl.from;
    s->to = pl.to;
    s->vol = pl.vol;
    s->repeat = repeat_on;
    s->redbook = cd.redbook;
    s->cd_mixer = pl.cd_mixer;
    s->sel = sel_track;
    s->focus = focus;
    s->mx = mouse_x;
    s->my = mouse_y;
}

static void switch_engine(int bits)
{
    appstate_t s;

    if (bits == ENGINE_BITS)
        return;
    if (!can_switch)
        return;                         /* controls are hidden anyway */
    fill_state(&s);
    s.engine = bits;
    s.switched = 1;
    if (state_save(opt.state, &s)) {
        error_box(" Engine ", "Cannot write the state file.");
        return;
    }
    /* analog play is left running in the drive: the other engine picks
     * it up from the busy bit and the Q-channel.  Digital play stops
     * here and restarts there. */
    if (pl.mode == MODE_DIGITAL && pl.state != PS_STOPPED)
        pl_stop();
    g_exitcode = bits;
    tui_switching = 1;
    g_quit = 1;
}

/* ---- commands ---------------------------------------------------------- */

static void cmd_exec(int cmd)
{
    int i;

    switch (cmd) {
    case C_INFO:   dlg_info(); break;
    case C_PEEK:   dlg_peek(); break;
    case C_SOUND:  dlg_sound(); break;
    case C_LOG:    dlg_log(); break;
    case C_EXIT:   g_quit = 1; break;
    case C_DRIVE_NEXT:
        pl_stop();
        for (i = 0; i < cd.ndrives; i++)
            if (cd.letters[i] == cd.cur) {
                cd_select(cd.letters[(i + 1) % cd.ndrives]);
                break;
            }
        sel_track = first_audio();
        break;
    case C_EJECT:  pl_stop(); cd_eject(); break;
    case C_CLOSE:  cd_close_tray(); cd_read_toc(); sel_track = first_audio(); break;
    case C_LOCK:   cd_lock(1); break;
    case C_UNLOCK: cd_lock(0); break;
    case C_RESET:  pl_stop(); cd_reset(); break;
    case C_TOC:    cd_read_toc(); sel_track = first_audio(); break;
    case C_PLAY:   play_track(sel_track ? sel_track : first_audio()); break;
    case C_PAUSE:  toggle_pause(); break;
    case C_STOP:   pl_stop(); break;
    case C_NEXT:   step_track(1); break;
    case C_PREV:   step_track(-1); break;
    case C_ANALOG: pl_set_mode(MODE_ANALOG); break;
    case C_DIGITAL: pl_set_mode(MODE_DIGITAL); break;
    case C_E16:    switch_engine(16); break;
    case C_E32:    switch_engine(32); break;
    case C_REPEAT: repeat_on = !repeat_on; break;
    case C_REDBOOK: cd.redbook = !cd.redbook; break;
    case C_CDMIX:  pl.cd_mixer = !pl.cd_mixer; pl_set_volume(pl.vol); break;
    case C_HELP:   dlg_help(); break;
    case C_ABOUT:  dlg_about(); break;
    }
}

static void activate(int id)
{
    switch (id) {
    case ID_LIST:    play_track(sel_track); break;
    case ID_PREV:    step_track(-1); break;
    case ID_REW:     seek_rel(-5 * 75L); break;
    case ID_PLAY:
        if (pl.state == PS_PAUSED) pl_resume();
        else play_track(sel_track ? sel_track : first_audio());
        break;
    case ID_PAUSE:   toggle_pause(); break;
    case ID_STOP:    pl_stop(); break;
    case ID_FFWD:    seek_rel(5 * 75L); break;
    case ID_NEXT:    step_track(1); break;
    case ID_EJECT:   cmd_exec(C_EJECT); break;
    case ID_ANALOG:  cmd_exec(C_ANALOG); break;
    case ID_DIGITAL: cmd_exec(C_DIGITAL); break;
    case ID_E16:     cmd_exec(C_E16); break;
    case ID_E32:     cmd_exec(C_E32); break;
    case ID_REPEAT:  cmd_exec(C_REPEAT); break;
    case ID_REDBOOK: cmd_exec(C_REDBOOK); break;
    }
}

static void set_vol_from_x(int mx)
{
    const ctl_t *c = &ctl[ID_VOL];
    long v = ((long)mx - c->x * 8) * 255 / (c->w * 8 - 1);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    pl_set_volume((u8)v);
}

static void seek_from_x(int mx)
{
    const ctl_t *c = &ctl[ID_SEEK];
    int t = pl.state != PS_STOPPED ? pl.track : sel_track;
    long px = (long)mx - c->x * 8;
    u32 f;

    if (!t || !track_is_audio(t))
        return;
    if (px < 0) px = 0;
    f = cd.toc.t[t].start + (u32)(px * (long)cd.toc.t[t].len / (c->w * 8));
    if (f >= cd.toc.t[t].start + cd.toc.t[t].len)
        f = cd.toc.t[t].start + cd.toc.t[t].len - 1;
    sel_track = t;
    pl_play(f, pl_audio_end(f));
}

static void list_move(int d)
{
    int t = sel_track + d;
    if (!cd.toc.valid)
        return;
    if (t < cd.toc.first) t = cd.toc.first;
    if (t > cd.toc.last) t = cd.toc.last;
    sel_track = t;
}

static void handle_key(u16 k)
{
    int c = key_ascii(k), a = alt_letter(k), i;

    if (k == K_ALT(SC_X)) { g_quit = 1; return; }
    if (k == K_F10) { cmd_exec(menu_run(0)); return; }
    if (k == K_F1) { dlg_help(); return; }
    for (i = 0; i < NMENU; i++)
        if (a && a == menu_alt[i]) {
            cmd_exec(menu_run(i));
            return;
        }
    for (i = 0; i < ID_N; i++)
        if (ctl[i].altkey && (k >> 8) == ctl[i].altkey && (k & 0xFF) == 0) {
            focus = i;
            if (ctl[i].type == CT_RADIO || ctl[i].type == CT_CHECK)
                activate(i);
            return;
        }
    if (k == K_TAB || k == K_STAB) {
        do
            focus = (focus + (k == K_TAB ? 1 : ID_N - 1)) % ID_N;
        while (ctl[focus].w == 0);         /* hidden */
        return;
    }

    /* keys for the focused control */
    switch (ctl[focus].type) {
    case CT_LIST:
        if (k == K_UP)   { list_move(-1); return; }
        if (k == K_DOWN) { list_move(1); return; }
        if (k == K_PGUP) { list_move(-LIST_H); return; }
        if (k == K_PGDN) { list_move(LIST_H); return; }
        if (k == K_HOME) { list_move(-99); return; }
        if (k == K_END)  { list_move(99); return; }
        if (k == K_ENTER) { activate(ID_LIST); return; }
        break;
    case CT_SLIDER:
        if (k == K_LEFT)  { pl_set_volume((u8)(pl.vol > 16 ? pl.vol - 16 : 0)); return; }
        if (k == K_RIGHT) { pl_set_volume((u8)(pl.vol < 239 ? pl.vol + 16 : 255)); return; }
        break;
    case CT_RADIO:
    case CT_CHECK:
        if (k == K_SPACE || k == K_ENTER) { activate(focus); return; }
        if (ctl[focus].type == CT_RADIO && (k == K_LEFT || k == K_RIGHT)) {
            int other = (focus == ID_ANALOG) ? ID_DIGITAL : (focus == ID_DIGITAL) ? ID_ANALOG :
                        (focus == ID_E16) ? ID_E32 : ID_E16;
            focus = other;
            activate(other);
            return;
        }
        break;
    case CT_ICON:
        if (k == K_ENTER || k == K_SPACE) { activate(focus); return; }
        if (k == K_LEFT && focus > ID_PREV) { focus--; return; }
        if (k == K_RIGHT && focus < ID_EJECT) { focus++; return; }
        break;
    }
    if (k == K_LEFT)  { seek_rel(-5 * 75L); return; }
    if (k == K_RIGHT) { seek_rel(5 * 75L); return; }
    if (k == K_ENTER) { activate(focus); return; }

    switch (c) {
    case 'p': cmd_exec(C_PLAY); break;
    case ' ': toggle_pause(); break;
    case 's': pl_stop(); break;
    case 'n': step_track(1); break;
    case 'b': step_track(-1); break;
    case 'e': cmd_exec(C_EJECT); break;
    case 'c': cmd_exec(C_CLOSE); break;
    case 'm': pl_set_mode(pl.mode == MODE_ANALOG ? MODE_DIGITAL : MODE_ANALOG); break;
    case 'i': dlg_info(); break;
    case 'r': dlg_peek(); break;
    case 'l': dlg_log(); break;
    default:
        if (c >= '1' && c <= '9')
            play_track(c - '0');
    }
}

static void handle_mouse(const event_t *e)
{
    int i;

    if (e->type == EV_MDOWN && e->button == 1) {
        if (e->cy == 0) {
            for (i = 0; i < NMENU; i++)
                if (e->cx >= menus[i].x &&
                    e->cx < menus[i].x + tui_hlen(menus[i].title) + 2) {
                    cmd_exec(menu_run(i));
                    return;
                }
            return;
        }
        if (e->cy == 24) {                  /* status line shortcuts */
            if (e->cx < 9) dlg_help();
            else if (e->cx < 19) cmd_exec(menu_run(0));
            else if (e->cx < 27) cmd_exec(C_PLAY);
            else if (e->cx < 39) toggle_pause();
            else if (e->cx < 47) pl_stop();
            else if (e->cx < 58) step_track(1);
            else if (e->cx < 66) pl_set_mode(pl.mode == MODE_ANALOG ? MODE_DIGITAL : MODE_ANALOG);
            else g_quit = 1;
            return;
        }
        if (e->cy == 2 && e->cx >= 2 && e->cx <= 4) { g_quit = 1; return; }
        for (i = 0; i < ID_N; i++) {
            const ctl_t *c = &ctl[i];
            if (!inside(c, e->cx, e->cy))
                continue;
            if (c->type != CT_SEEK)
                focus = i;
            switch (c->type) {
            case CT_LIST:
                if (e->cx == c->x + LIST_W) {           /* scrollbar */
                    if (e->cy == c->y) list_move(-1);
                    else if (e->cy == c->y + LIST_H - 1) list_move(1);
                    else list_move(e->cy < c->y + LIST_H / 2 ? -LIST_H : LIST_H);
                } else if (cd.toc.valid) {
                    int t = cd.toc.first + list_top + (e->cy - c->y);
                    if (t <= cd.toc.last) {
                        sel_track = t;
                        if (e->dbl)
                            play_track(t);
                    }
                }
                break;
            case CT_ICON:
                pressed = i;
                break;
            case CT_RADIO:
            case CT_CHECK:
                activate(i);
                break;
            case CT_SLIDER:
                dragging = i;
                set_vol_from_x(e->mx);
                break;
            case CT_SEEK:
                seek_from_x(e->mx);
                break;
            }
            return;
        }
    } else if (e->type == EV_MUP && e->button == 1) {
        if (pressed >= 0 && inside(&ctl[pressed], e->cx, e->cy))
            activate(pressed);
        pressed = -1;
        dragging = -1;
    } else if (e->type == EV_MMOVE && dragging == ID_VOL && (e->held & 1)) {
        set_vol_from_x(e->mx);
    } else if (e->type == EV_MDOWN && e->button == 2) {
        toggle_pause();
    }
}

/* ---- entry ------------------------------------------------------------ */

void ui_break(void)
{
    g_quit = 1;
}

void ui_shutdown(void)
{
    if (ui_up) {
        ui_up = 0;
        tui_exit();
    }
}

int ui_run(int r, u32 t0)
{
    appstate_t st;
    event_t e;
    int have_state;

    start_tick = t0;
    state_defaults(&st);
    have_state = state_load(opt.state, &st) == 0;
    /* /D:x was applied by main; otherwise the drive used last */
    if (opt.drive < 0 && have_state && st.drive >= 0 && cd.present && st.drive != cd.cur) {
        int i;
        for (i = 0; i < cd.ndrives; i++)
            if (cd.letters[i] == st.drive)
                cd_select(st.drive);
    }
    if (have_state) {
        pl.mode = st.mode;
        pl.vol = (u8)st.vol;
        pl.cd_mixer = st.cd_mixer;
        repeat_on = st.repeat;
        cd.redbook = st.redbook || opt.redbook;
        sel_track = st.sel;
        focus = st.focus >= 0 && st.focus < ID_N ? st.focus : 0;
    }
    if (opt.mode >= 0)
        pl.mode = opt.mode;
    /* the switch needs CDPL.EXE to start the other engine, and a 386 for
     * the 32-bit one; otherwise the Engine row is plain text */
    can_switch = opt.launched && cpu_is_386();
    if (!can_switch) {
        ctl[ID_E16].w = ctl[ID_E32].w = 0;
        ctl[ID_E16].altkey = ctl[ID_E32].altkey = 0;
        menus[3].n = M_OPT_SOLO;
        if (focus == ID_E16 || focus == ID_E32)
            focus = 0;
    }
    if (!sel_track || !cd.toc.valid || sel_track < cd.toc.first || sel_track > cd.toc.last)
        sel_track = first_audio();

    tui_init(!opt.nofont, opt.biosfont, opt.mabs, opt.launched && have_state && st.switched);
    ui_up = 1;
    tui_script(opt.keys);

    if (r < 0) {
        error_box(" CDPL ", r == -2 ? "MSCDEX is not installed (or reports no CD drives)."
                                    : "Cannot allocate DOS memory.");
        return 2;
    }

    /* pick up where the other engine left off */
    if (have_state && st.switched) {
        pl.from = st.from;
        pl.to = st.to;
        pl.pos = st.pos;
        mouse_setpos(st.mx, st.my);     /* pointer stays where it was */
        if (pl.mode == MODE_ANALOG) {
            pl_resync();
            if (st.state == PS_PLAYING && pl.state != PS_PLAYING)
                cd_log(0xF1, 0, cd.last_status, 0, "switch: drive no longer busy");
        } else if (st.state != PS_STOPPED && st.to > st.pos) {
            pl_play(st.pos, st.to);
            if (st.state == PS_PAUSED)
                pl_pause();
        }
        cd_log(0xF1, 0, 0, 0, "engine switch to %d-bit, was %s", ENGINE_BITS,
               st.state == PS_PLAYING ? "playing" : st.state == PS_PAUSED ? "paused" : "stopped");
        st.switched = 0;
        fill_state(&st);
        state_save(opt.state, &st);
    } else if (pl.mode == MODE_ANALOG) {
        pl_resync();                    /* the drive may already be playing */
    }

    while (!g_quit) {
        draw_main();
        tui_flush();
        idle();
        while (!g_quit && tui_event(&e)) {
            if (e.type == EV_KEY)
                handle_key(e.key);
            else
                handle_mouse(&e);
        }
    }

    if (g_exitcode == 0) {
        pl_stop();
        fill_state(&st);
        st.state = PS_STOPPED;
        state_save(opt.state, &st);
    }
    ui_shutdown();
    return g_exitcode;
}
