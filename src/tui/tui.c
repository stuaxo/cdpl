/* tui.c - back buffer, Norton-style drawing, pixel mouse pointer, events */
#include <ctype.h>
#include <stdlib.h>
#include "tui.h"
#include "vga.h"
#include "mouse.h"
#include "glyphs.h"

int tui_glyphs;
u16 tui_buf[SCR_H * SCR_W];

static u16 frame[SCR_H * SCR_W];     /* back buffer + pointer */
static u16 shadow[SCR_H * SCR_W];    /* what is on screen */
static int first_flush;
static u32 reassert_at;              /* tick for the one mouse re-reset */
static int driver_cursor;

/* pointer compositing state */
static int  ptr_set;
static int  ptr_lastx = -1, ptr_lasty = -1;
static u16  ptr_under[6];

int tui_switching;                   /* exit leaves the video mode as is */

int tui_init(int want_font, int bios_font, int mouse_abs, int resume)
{
    vga_init(want_font, bios_font, resume);
    tui_glyphs = vga_fontmode;
    driver_cursor = !tui_glyphs;
    mouse_absolute = mouse_abs;
    mouse_init(driver_cursor);
    reassert_at = bios_ticks() + 18;
    first_flush = 1;
    return 0;
}

void tui_exit(void)
{
    mouse_hide();
    vga_exit(tui_switching);
    mouse_exit();
}

/* ---- drawing --------------------------------------------------------- */

void tui_put(int x, int y, u8 ch, u8 attr)
{
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H)
        tui_buf[y * SCR_W + x] = (u16)(ch | (attr << 8));
}

void tui_attr(int x, int y, u8 attr)
{
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H)
        tui_buf[y * SCR_W + x] = (u16)((tui_buf[y * SCR_W + x] & 0xFF) | (attr << 8));
}

u16 tui_get(int x, int y)
{
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H)
        return tui_buf[y * SCR_W + x];
    return 0;
}

void tui_str(int x, int y, const char *s, u8 attr)
{
    while (*s)
        tui_put(x++, y, (u8)*s++, attr);
}

int tui_hstr(int x, int y, const char *s, u8 attr, u8 hot)
{
    int on = 0, n = 0;

    for (; *s; s++) {
        if (*s == '~') {
            on = !on;
            continue;
        }
        tui_put(x + n++, y, (u8)*s, on ? hot : attr);
    }
    return n;
}

int tui_hlen(const char *s)
{
    int n = 0;

    for (; *s; s++)
        if (*s != '~')
            n++;
    return n;
}

void tui_fill(int x, int y, int w, int h, u8 ch, u8 attr)
{
    int i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            tui_put(x + i, y + j, ch, attr);
}

void tui_box(int x, int y, int w, int h, u8 attr, int dbl)
{
    int i;
    u8 hz = (u8)(dbl ? DB_H : BX_H), vt = (u8)(dbl ? DB_V : BX_V);

    for (i = 1; i < w - 1; i++) {
        tui_put(x + i, y, hz, attr);
        tui_put(x + i, y + h - 1, hz, attr);
    }
    for (i = 1; i < h - 1; i++) {
        tui_put(x, y + i, vt, attr);
        tui_put(x + w - 1, y + i, vt, attr);
    }
    tui_put(x, y, (u8)(dbl ? DB_TL : BX_TL), attr);
    tui_put(x + w - 1, y, (u8)(dbl ? DB_TR : BX_TR), attr);
    tui_put(x, y + h - 1, (u8)(dbl ? DB_BL : BX_BL), attr);
    tui_put(x + w - 1, y + h - 1, (u8)(dbl ? DB_BR : BX_BR), attr);
}

/* Norton shadow: keep the characters, darken them */
void tui_shadow(int x, int y, int w, int h)
{
    int i;

    for (i = 1; i <= h; i++) {
        tui_attr(x + w, y + i, A_SHADOW);
        tui_attr(x + w + 1, y + i, A_SHADOW);
    }
    for (i = 2; i < w + 2; i++)
        tui_attr(x + i, y + h, A_SHADOW);
}

void tui_window(int x, int y, int w, int h, const char *title, u8 attr, u8 fattr)
{
    int tl;

    tui_fill(x, y, w, h, ' ', attr);
    tui_box(x, y, w, h, fattr, 1);
    if (tui_glyphs)
        tui_put(x + 2, y, G_CLOSE, fattr);
    else {
        tui_put(x + 1, y, '[', fattr);
        tui_put(x + 2, y, 0xFE, fattr);
        tui_put(x + 3, y, ']', fattr);
    }
    if (title && *title) {
        tl = (int)strlen(title);
        tui_put(x + (w - tl) / 2 - 1, y, ' ', A_WIN_TITLE);
        tui_str(x + (w - tl) / 2, y, title, A_WIN_TITLE);
        tui_put(x + (w - tl) / 2 + tl, y, ' ', A_WIN_TITLE);
    }
}

/* a bar of 'cells' characters showing val/max at pixel resolution */
void tui_bar(int x, int y, int cells, u32 val, u32 max, u8 attr)
{
    u32 px = max ? (val >= max ? (u32)cells * 8 : val * (u32)cells * 8 / max) : 0;
    int i;

    for (i = 0; i < cells; i++) {
        u32 lo = (u32)i * 8;
        int k = px >= lo + 8 ? 8 : px > lo ? (int)(px - lo) : 0;
        if (tui_glyphs)
            tui_put(x + i, y, (u8)(G_FILL0 + k), attr);
        else
            tui_put(x + i, y, (u8)(k >= 4 ? 0xDB : k ? 0xDD : 0xB0), attr);
    }
}

void tui_save(u16 *dst) { memcpy(dst, tui_buf, sizeof(tui_buf)); }
void tui_restore(const u16 *src) { memcpy(tui_buf, src, sizeof(tui_buf)); }

u8 g_check(int on) { return tui_glyphs ? (u8)(on ? G_CHK_ON : G_CHK_OFF) : (u8)(on ? 'X' : ' '); }
u8 g_radio(int on) { return tui_glyphs ? (u8)(on ? G_RAD_ON : G_RAD_OFF) : (u8)(on ? 0x07 : ' '); }

void tui_icon(int x, int y, int icon, u8 attr)
{
    static const char *fb[8] = { "|\x11", "\x11\x11", "\x10 ", "\xBA ",
                                 "\xFE ", "\x10\x10", "\x10|", "\x1E " };
    if (tui_glyphs) {
        tui_put(x, y, (u8)G_ICON(icon), attr);
        tui_put(x + 1, y, (u8)(G_ICON(icon) + 1), attr);
    } else {
        tui_put(x, y, (u8)fb[icon][0], attr);
        tui_put(x + 1, y, (u8)fb[icon][1], attr);
    }
}

/* ---- pointer ----------------------------------------------------------- */

/* Build glyphs for the 3x2 cells under the pointer: the glyphs that are
 * there, with the arrow drawn in at the pixel offset.  Two sets of
 * slots alternate so the cells on screen never show a half-built
 * glyph. */
static void compose_pointer(void)
{
    int cx = mouse_x >> 3, cy = mouse_y >> 4;
    int ox = mouse_x & 7, oy = mouse_y & 15;
    u32 rows[32];
    u16 under[6];
    int dx, dy, r, k, same;
    u8 base;

    for (k = 0; k < 6; k++) {
        int x = cx + k % 3, y = cy + k / 3;
        under[k] = (x < SCR_W && y < SCR_H) ? frame[y * SCR_W + x] : 0;
    }
    same = mouse_x == ptr_lastx && mouse_y == ptr_lasty &&
           memcmp(under, ptr_under, sizeof(under)) == 0;
    base = (u8)(ptr_set ? G_PTR_B : G_PTR_A);
    if (!same) {
        ptr_set ^= 1;
        base = (u8)(ptr_set ? G_PTR_B : G_PTR_A);
        memset(rows, 0, sizeof(rows));
        for (dy = 0; dy < 2; dy++)
            for (dx = 0; dx < 3; dx++) {
                u8 ch = (u8)under[dy * 3 + dx];
                for (r = 0; r < 16; r++)
                    rows[dy * 16 + r] |= (u32)vga_font[ch * 16 + r] << (16 - dx * 8);
            }
        for (r = 0; r < 16 && oy + r < 32; r++) {
            u32 m = ((u32)ptr_mask[r] << 8) >> ox;
            u32 im = ((u32)ptr_image[r] << 8) >> ox;
            rows[oy + r] = (rows[oy + r] & ~m) | im;
        }
        for (k = 0; k < 6; k++) {
            dx = k % 3;
            dy = k / 3;
            for (r = 0; r < 16; r++)
                vga_font[(base + k) * 16 + r] = (u8)(rows[dy * 16 + r] >> (16 - dx * 8));
        }
        vga_upload(base, 6);
        ptr_lastx = mouse_x;
        ptr_lasty = mouse_y;
        memcpy(ptr_under, under, sizeof(under));
    }
    for (k = 0; k < 6; k++) {
        int x = cx + k % 3, y = cy + k / 3;
        if (x < SCR_W && y < SCR_H)
            frame[y * SCR_W + x] = (u16)((under[k] & 0xFF00) | (base + k));
    }
}

void tui_flush(void)
{
    u16 FAR *v = video_ptr();
    int i, changed = 0;

    memcpy(frame, tui_buf, sizeof(frame));
    if (tui_glyphs && mouse_present)
        compose_pointer();
    for (i = 0; i < SCR_W * SCR_H; i++)
        if (first_flush || frame[i] != shadow[i]) {
            changed = 1;
            break;
        }
    if (!changed)
        return;
    if (driver_cursor && mouse_present) {
        rmregs_t r;
        rm_clear(&r); r.eax = 2; rm_int(0x33, &r);
    }
    for (; i < SCR_W * SCR_H; i++)
        if (first_flush || frame[i] != shadow[i]) {
            v[i] = frame[i];
            shadow[i] = frame[i];
        }
    first_flush = 0;
    if (driver_cursor && mouse_present) {
        rmregs_t r;
        rm_clear(&r); r.eax = 1; rm_int(0x33, &r);
    }
}

/* ---- events ---------------------------------------------------------- */

static int  last_buttons;
static u32  last_click_tick;
static int  last_click_cx = -1, last_click_cy = -1;

int key_ascii(u16 k)
{
    int c = k & 0xFF;
    return (c >= 32 && c < 127) ? tolower(c) : 0;
}

int alt_letter(u16 k)
{
    static const char row1[] = "qwertyuiop";
    static const char row2[] = "asdfghjkl";
    static const char row3[] = "zxcvbnm";
    int sc = k >> 8;

    if ((k & 0xFF) != 0)
        return 0;
    if (sc >= 0x10 && sc <= 0x19) return row1[sc - 0x10];
    if (sc >= 0x1E && sc <= 0x26) return row2[sc - 0x1E];
    if (sc >= 0x2C && sc <= 0x32) return row3[sc - 0x2C];
    return 0;
}

/* ---- scripted input (/KEYS:) ------------------------------------------
 * comma separated tokens: a single character, a key name (tab stab enter
 * esc space up down left right pgup pgdn home end f1 f10 alt-x), wN to
 * wait N ticks, mX:Y to move the pointer to pixel X,Y, click, rclick */
static const char *script;
static u32 script_wait;
static int script_up;               /* pending button release */

void tui_script(const char *s) { script = s && *s ? s : 0; }

static const struct { const char *name; u16 key; } keynames[] = {
    { "tab", K_TAB }, { "stab", K_STAB }, { "enter", K_ENTER },
    { "esc", K_ESC }, { "space", K_SPACE }, { "up", K_UP },
    { "down", K_DOWN }, { "left", K_LEFT }, { "right", K_RIGHT },
    { "pgup", K_PGUP }, { "pgdn", K_PGDN }, { "home", K_HOME },
    { "end", K_END }, { "f1", K_F1 }, { "f10", K_F10 } };

static int script_event(event_t *e)
{
    static const char alt_sc[] = "\x1E\x30\x2E\x20\x12\x21\x22\x23\x17\x24"
                                 "\x25\x26\x32\x31\x18\x19\x10\x13\x1F\x14"
                                 "\x16\x2F\x11\x2D\x15\x2C";
    char tok[16];
    int n = 0, i;

    if (script_up) {
        e->type = EV_MUP;
        e->button = script_up;
        script_up = 0;
        goto pos;
    }
    if (!script || bios_ticks() < script_wait)
        return 0;
    while (*script && *script != ',' && n < 15)
        tok[n++] = *script++;
    tok[n] = 0;
    if (*script == ',')
        script++;
    if (!*script)
        script = 0;
    if (tok[0] == 'w' && tok[1] >= '0' && tok[1] <= '9') {
        script_wait = bios_ticks() + (u32)atoi(tok + 1);
        return 0;
    }
    if (tok[0] == 'm' && tok[1] >= '0' && tok[1] <= '9') {
        char *c = strchr(tok, ':');
        /* through the driver too, or the resync in mouse_poll would
         * put the pointer back */
        mouse_setpos(atoi(tok + 1), c ? atoi(c + 1) : mouse_y);
        e->type = EV_MMOVE;
        goto pos;
    }
    if (!strcmp(tok, "click") || !strcmp(tok, "rclick")) {
        e->type = EV_MDOWN;
        e->button = tok[0] == 'r' ? 2 : 1;
        script_up = e->button;
        goto pos;
    }
    e->type = EV_KEY;
    if (n == 1) {
        e->key = (u16)(u8)tok[0];
        return 1;
    }
    if (!strncmp(tok, "alt-", 4) && tok[4] >= 'a' && tok[4] <= 'z') {
        e->key = K_ALT(alt_sc[tok[4] - 'a']);
        return 1;
    }
    for (i = 0; i < (int)(sizeof(keynames) / sizeof(keynames[0])); i++)
        if (!strcmp(tok, keynames[i].name)) {
            e->key = keynames[i].key;
            return 1;
        }
    return 0;
pos:
    e->mx = mouse_x;
    e->my = mouse_y;
    e->cx = mouse_x >> 3;
    e->cy = mouse_y >> 4;
    return 1;
}

int tui_event(event_t *e)
{
    rmregs_t r;
    int b;

    memset(e, 0, sizeof(*e));
    /* about a second after start-up, reset the driver once more.  Under
     * dosemu2 the host pointer can come back after the first reset (seen
     * after a 16->32 switch under Wayland); a reset tells the host the
     * cursor is hidden again.  Our own position is kept. */
    if (reassert_at && bios_ticks() >= reassert_at) {
        reassert_at = 0;
        mouse_init(driver_cursor);
    }
    if (script_event(e))
        return 1;
    rm_clear(&r);
    r.eax = 0x0100;
    rm_int(0x16, &r);
    if (!(r.flags & 0x40)) {                /* ZF clear: key waiting */
        rm_clear(&r);
        r.eax = 0x0000;
        rm_int(0x16, &r);
        e->type = EV_KEY;
        e->key = (u16)r.eax;
        if ((e->key & 0xFF) == 0xE0)
            e->key &= 0xFF00;
        return 1;
    }
    if (!mouse_poll() && mouse_buttons == last_buttons)
        return 0;
    e->mx = mouse_x;
    e->my = mouse_y;
    e->cx = mouse_x >> 3;
    e->cy = mouse_y >> 4;
    b = mouse_buttons;
    e->held = b;
    if ((b & 1) && !(last_buttons & 1)) {
        u32 now = bios_ticks();
        e->type = EV_MDOWN;
        e->button = 1;
        e->dbl = now - last_click_tick < 8 && e->cx == last_click_cx &&
                 e->cy == last_click_cy;
        last_click_tick = e->dbl ? 0 : now;
        last_click_cx = e->cx;
        last_click_cy = e->cy;
    } else if ((b & 2) && !(last_buttons & 2)) {
        e->type = EV_MDOWN;
        e->button = 2;
    } else if (!(b & 1) && (last_buttons & 1)) {
        e->type = EV_MUP;
        e->button = 1;
    } else if (!(b & 2) && (last_buttons & 2)) {
        e->type = EV_MUP;
        e->button = 2;
    } else {
        e->type = EV_MMOVE;
    }
    last_buttons = b;
    return 1;
}
