/* tui.h - text UI primitives: back buffer, boxes, pointer, events */
#ifndef TUI_H
#define TUI_H
#include "plat.h"

#define SCR_W 80
#define SCR_H 25

/* colours (Norton-like palette) */
#define A_DESK      0x13    /* desktop pattern */
#define A_MENU      0x70
#define A_MENU_HOT  0x74
#define A_MENU_SEL  0x07
#define A_MENU_SELH 0x0E
#define A_WIN       0x70    /* window body */
#define A_WIN_FRAME 0x7F
#define A_WIN_TITLE 0x1F
#define A_LABEL     0x70
#define A_LABEL_HOT 0x74
#define A_FOCUS     0x7F
#define A_LIST      0x1F
#define A_LIST_SEL  0x30
#define A_LIST_SELF 0x3F
#define A_LIST_HDR  0x1E
#define A_LCD       0x0A
#define A_LCD_DIM   0x02
#define A_LCD_HI    0x0E
#define A_BTN       0x30
#define A_BTN_HOT   0x34
#define A_BTN_FOC   0x3F
#define A_BTN_DOWN  0x1F
#define A_SHADOW    0x08
#define A_STATUS    0x30
#define A_STATUS_K  0x3F
#define A_LOG       0x1B
#define A_LOG_ERR   0x1C
#define A_DLG       0x3F
#define A_DLG_TEXT  0x30
#define A_BAR       0x7B    /* seek bar: bright cyan on grey */
#define A_BAR_FOC   0x79

extern int tui_glyphs;          /* custom font active */
extern u16 tui_buf[SCR_H * SCR_W];

/* box-drawing chars, or fallbacks */
#define BX_H   0xC4
#define BX_V   0xB3
#define BX_TL  0xDA
#define BX_TR  0xBF
#define BX_BL  0xC0
#define BX_BR  0xD9
#define DB_H   0xCD
#define DB_V   0xBA
#define DB_TL  0xC9
#define DB_TR  0xBB
#define DB_BL  0xC8
#define DB_BR  0xBC

extern int tui_switching;          /* set before exit for an engine switch */
int   tui_init(int want_font, int bios_font, int mouse_abs, int resume);
void  tui_exit(void);
void  tui_put(int x, int y, u8 ch, u8 attr);
void  tui_attr(int x, int y, u8 attr);
u16   tui_get(int x, int y);
void  tui_str(int x, int y, const char *s, u8 attr);
int   tui_hstr(int x, int y, const char *s, u8 attr, u8 hot);  /* ~X~ hotkeys */
int   tui_hlen(const char *s);
void  tui_fill(int x, int y, int w, int h, u8 ch, u8 attr);
void  tui_box(int x, int y, int w, int h, u8 attr, int dbl);
void  tui_shadow(int x, int y, int w, int h);
void  tui_window(int x, int y, int w, int h, const char *title, u8 attr, u8 fattr);
void  tui_bar(int x, int y, int cells, u32 val, u32 max, u8 attr);
void  tui_save(u16 *dst);
void  tui_restore(const u16 *src);
void  tui_flush(void);

/* glyph helpers that fall back to CP437 without the custom font */
u8    g_check(int on);
u8    g_radio(int on);
void  tui_icon(int x, int y, int icon, u8 attr);

/* events */
enum { EV_NONE, EV_KEY, EV_MDOWN, EV_MUP, EV_MMOVE };
typedef struct {
    int type;
    u16 key;          /* scan<<8 | ascii (ascii 0 for extended keys) */
    int mx, my;       /* pixels */
    int cx, cy;       /* cell */
    int button;       /* 1 left, 2 right */
    int dbl;          /* double click */
    int held;         /* buttons down during a move */
} event_t;
int   tui_event(event_t *e);
void  tui_script(const char *s);   /* /KEYS: synthetic input */

#define K_ESC    0x011B
#define K_ENTER  0x1C0D
#define K_TAB    0x0F09
#define K_STAB   0x0F00
#define K_SPACE  0x3920
#define K_UP     0x4800
#define K_DOWN   0x5000
#define K_LEFT   0x4B00
#define K_RIGHT  0x4D00
#define K_HOME   0x4700
#define K_END    0x4F00
#define K_PGUP   0x4900
#define K_PGDN   0x5100
#define K_F1     0x3B00
#define K_F10    0x4400
#define K_ALT(scan) ((u16)((scan) << 8))
#define SC_X 0x2D
#define SC_F 0x21
#define SC_D 0x20
#define SC_P 0x19
#define SC_O 0x18
#define SC_H 0x23

int   key_ascii(u16 k);       /* lower-case ascii or 0 */
int   alt_letter(u16 k);      /* 'a'..'z' for Alt+letter, else 0 */

#endif
