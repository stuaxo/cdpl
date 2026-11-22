/* glyphs.c - bitmaps for the redefined characters, as ASCII art */
#include <string.h>
#include "glyphs.h"

/* 8x16 cells: 'X' = foreground pixel */
static const char *chk_off[16] = {
    "........", "........", "........", "XXXXXXX.", "X.....X.", "X.....X.",
    "X.....X.", "X.....X.", "X.....X.", "X.....X.", "X.....X.", "XXXXXXX.",
    "........", "........", "........", "........" };
static const char *chk_on[16] = {
    "........", "........", "........", "XXXXXXX.", "X.....X.", "X....XX.",
    "X....XX.", "X...XXX.", "XX.XX.X.", "XXXXX.X.", "X.XX..X.", "XXXXXXX.",
    "........", "........", "........", "........" };
static const char *rad_off[16] = {
    "........", "........", "........", "..XXX...", ".X...X..", "X.....X.",
    "X.....X.", "X.....X.", "X.....X.", "X.....X.", ".X...X..", "..XXX...",
    "........", "........", "........", "........" };
static const char *rad_on[16] = {
    "........", "........", "........", "..XXX...", ".X...X..", "X.....X.",
    "X.XXX.X.", "X.XXX.X.", "X.XXX.X.", "X.....X.", ".X...X..", "..XXX...",
    "........", "........", "........", "........" };
static const char *close_box[16] = {
    "........", "........", "XXXXXXXX", "X......X", "X......X", "X.X..X.X",
    "X..XX..X", "X..XX..X", "X.X..X.X", "X......X", "X......X", "XXXXXXXX",
    "........", "........", "........", "........" };
static const char *note[16] = {
    "........", "........", "...XXXX.", "...X..X.", "...XXXX.", "...X..X.",
    "...X..X.", "...X..X.", "...X..X.", ".XXX.XX.", "XXXXXXX.", "XXX.XXX.",
    ".X...X..", "........", "........", "........" };
static const char *disc[16] = {
    "........", "........", "..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXX..XXX",
    "XX....XX", "XX....XX", "XXX..XXX", "XXXXXXXX", ".XXXXXX.", "..XXXX..",
    "........", "........", "........", "........" };

/* 16x16 icons, rows 0-15 */
static const char *icons[8][16] = {
    { /* prev |<< */
    "................", "................", "...XX.....X.....", "...XX....XX.....",
    "...XX...XXX.....", "...XX..XXXX.....", "...XX.XXXXX.....", "...XXXXXXXX.....",
    "...XXXXXXXX.....", "...XX.XXXXX.....", "...XX..XXXX.....", "...XX...XXX.....",
    "...XX....XX.....", "...XX.....X.....", "................", "................" },
    { /* rewind << */
    "................", "................", "......X.....X...", ".....XX....XX...",
    "....XXX...XXX...", "...XXXX..XXXX...", "..XXXXX.XXXXX...", ".XXXXXXXXXXXX...",
    ".XXXXXXXXXXXX...", "..XXXXX.XXXXX...", "...XXXX..XXXX...", "....XXX...XXX...",
    ".....XX....XX...", "......X.....X...", "................", "................" },
    { /* play > */
    "................", "................", ".....X..........", ".....XX.........",
    ".....XXX........", ".....XXXX.......", ".....XXXXX......", ".....XXXXXX.....",
    ".....XXXXXX.....", ".....XXXXX......", ".....XXXX.......", ".....XXX........",
    ".....XX.........", ".....X..........", "................", "................" },
    { /* pause || */
    "................", "................", "....XXX..XXX....", "....XXX..XXX....",
    "....XXX..XXX....", "....XXX..XXX....", "....XXX..XXX....", "....XXX..XXX....",
    "....XXX..XXX....", "....XXX..XXX....", "....XXX..XXX....", "....XXX..XXX....",
    "....XXX..XXX....", "....XXX..XXX....", "................", "................" },
    { /* stop [] */
    "................", "................", "................", "....XXXXXXXX....",
    "....XXXXXXXX....", "....XXXXXXXX....", "....XXXXXXXX....", "....XXXXXXXX....",
    "....XXXXXXXX....", "....XXXXXXXX....", "....XXXXXXXX....", "....XXXXXXXX....",
    "................", "................", "................", "................" },
    { /* fast forward >> */
    "................", "................", "...X.....X......", "...XX....XX.....",
    "...XXX...XXX....", "...XXXX..XXXX...", "...XXXXX.XXXXX..", "...XXXXXXXXXXXX.",
    "...XXXXXXXXXXXX.", "...XXXXX.XXXXX..", "...XXXX..XXXX...", "...XXX...XXX....",
    "...XX....XX.....", "...X.....X......", "................", "................" },
    { /* next >>| */
    "................", "................", ".....X.....XX...", ".....XX....XX...",
    ".....XXX...XX...", ".....XXXX..XX...", ".....XXXXX.XX...", ".....XXXXXXXX...",
    ".....XXXXXXXX...", ".....XXXXX.XX...", ".....XXXX..XX...", ".....XXX...XX...",
    ".....XX....XX...", ".....X.....XX...", "................", "................" },
    { /* eject */
    "................", "................", ".......XX.......", "......XXXX......",
    ".....XXXXXX.....", "....XXXXXXXX....", "...XXXXXXXXXX...", "..XXXXXXXXXXXX..",
    "..XXXXXXXXXXXX..", "................", "................", "..XXXXXXXXXXXX..",
    "..XXXXXXXXXXXX..", "................", "................", "................" },
};

/* mouse arrow, 'X' outline (background colour), 'o' body (foreground) */
static const char *arrow[16] = {
    "X...........", "XX..........", "XoX.........", "XooX........",
    "XoooX.......", "XooooX......", "XoooooX.....", "XooooooX....",
    "XoooooooX...", "XooooooooX..", "XoooooXXXXX.", "XooXooX.....",
    "XoX.XooX....", "XX..XooX....", "X....XooX...", ".....XXXX..." };

unsigned short ptr_mask[16];
unsigned short ptr_image[16];

static unsigned char row8(const char *s, int off)
{
    unsigned char b = 0;
    int i;

    for (i = 0; i < 8; i++)
        if (s[off + i] == 'X')
            b |= (unsigned char)(0x80 >> i);
    return b;
}

static void put8(unsigned char *font, int ch, const char **art)
{
    int r;

    for (r = 0; r < 16; r++)
        font[ch * 16 + r] = row8(art[r], 0);
}

void glyphs_build(unsigned char *font)
{
    int r, k, i;

    put8(font, G_CHK_OFF, chk_off);
    put8(font, G_CHK_ON, chk_on);
    put8(font, G_RAD_OFF, rad_off);
    put8(font, G_RAD_ON, rad_on);
    put8(font, G_CLOSE, close_box);
    put8(font, G_NOTE, note);
    put8(font, G_DISC, disc);

    /* bar fills: k pixels of solid bar (rows 3-12) then a dotted
     * track line (rows 7-8) for the rest of the cell */
    for (k = 0; k <= 8; k++) {
        unsigned char bar = (unsigned char)(0xFF00 >> k);
        for (r = 0; r < 16; r++) {
            unsigned char v = 0;
            if (r >= 3 && r <= 12)
                v = bar;
            if (r == 7)
                v |= 0x55;
            if (r == 8)
                v |= 0xAA;
            font[(G_FILL0 + k) * 16 + r] = v;
        }
    }

    for (i = 0; i < 8; i++)
        for (r = 0; r < 16; r++) {
            font[(G_ICON(i)) * 16 + r] = row8(icons[i][r], 0);
            font[(G_ICON(i) + 1) * 16 + r] = row8(icons[i][r], 8);
        }

    for (r = 0; r < 16; r++) {
        unsigned short m = 0, im = 0;
        for (i = 0; i < 12; i++) {
            if (arrow[r][i] == 'X' || arrow[r][i] == 'o')
                m |= (unsigned short)(0x8000 >> i);
            if (arrow[r][i] == 'o')
                im |= (unsigned short)(0x8000 >> i);
        }
        ptr_mask[r] = m;
        ptr_image[r] = im;
    }
}
