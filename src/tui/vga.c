/* vga.c - Norton-style text mode: 8-dot character clock and a font with
 * redefined characters for widgets and a pixel mouse pointer */
#include <conio.h>
#include <i86.h>
#include "vga.h"
#include "glyphs.h"

int vga_present;
int vga_fontmode;
int vga_biosfont;
u8  vga_font[256 * 16];

static dosbuf_t fbuf;               /* for INT 10h 1110h uploads */

int vga_detect(void)
{
    rmregs_t r;

    rm_clear(&r);
    r.eax = 0x1A00;
    rm_int(0x10, &r);
    vga_present = (r.eax & 0xFF) == 0x1A && ((r.ebx & 0xFF) == 7 || (r.ebx & 0xFF) == 8);
    return vga_present;
}

static void set_mode3(void)
{
    rmregs_t r;

    rm_clear(&r);
    r.eax = 0x0003;
    rm_int(0x10, &r);
}

static void cursor_hide(int hide)
{
    rmregs_t r;

    rm_clear(&r);
    r.eax = 0x0100;
    r.ecx = hide ? 0x2000 : 0x0D0E;
    rm_int(0x10, &r);
}

static void blink_off(void)
{
    rmregs_t r;

    rm_clear(&r);
    r.eax = 0x1003;                 /* bright backgrounds, no blink */
    r.ebx = 0;
    rm_int(0x10, &r);
}

static void read_rom_font(void)
{
    rmregs_t r;
    u8 FAR *src;

    rm_clear(&r);
    r.eax = 0x1130;
    r.ebx = 0x0600;                 /* BH=6: ROM 8x16 */
    rm_int(0x10, &r);
    src = lowmem_ptr(r.es, (u16)r.ebp);
    fmemcpy(vga_font, src, sizeof(vga_font));
}

/* 8 pixel wide characters: 25 MHz dot clock, seq reg 1 bit 0, no pel
 * panning.  Box drawing then joins seamlessly and our glyphs are not
 * stretched to a 9th column. */
static void set_8dot(void)
{
    u8 v;

    _disable();
    outpw(0x3C4, 0x0100);           /* synchronous reset */
    outp(0x3C2, (u8)(inp(0x3CC) & 0xF3));
    outp(0x3C4, 0x01);
    v = (u8)inp(0x3C5);
    outp(0x3C5, (u8)(v | 0x01));
    outpw(0x3C4, 0x0300);
    inp(0x3DA);
    outp(0x3C0, 0x13 | 0x20);
    outp(0x3C0, 0x00);
    _enable();
}

void vga_wait_retrace(void)
{
    unsigned n;

    for (n = 0; (inp(0x3DA) & 8) && n < 20000; n++)
        ;
    for (n = 0; !(inp(0x3DA) & 8) && n < 20000; n++)
        ;
}

static void plane2_begin(void)
{
    _disable();
    outpw(0x3C4, 0x0402);           /* map mask: plane 2 */
    outpw(0x3C4, 0x0704);           /* sequential, no odd/even */
    outpw(0x3CE, 0x0204);           /* read map 2 */
    outpw(0x3CE, 0x0005);           /* write mode 0 */
    outpw(0x3CE, 0x0406);           /* A000 64K, no odd/even */
}

static void plane2_end(void)
{
    outpw(0x3C4, 0x0302);
    outpw(0x3C4, 0x0304);
    outpw(0x3CE, 0x0004);
    outpw(0x3CE, 0x1005);
    outpw(0x3CE, 0x0E06);
    _enable();
}

void vga_upload(int first, int count)
{
    int c;

    if (!vga_fontmode)
        return;
    if (vga_biosfont) {
        rmregs_t r;
        fmemcpy(fbuf.p, vga_font + first * 16, count * 16);
        rm_clear(&r);
        r.eax = 0x1110;
        r.ebx = 0x1000;             /* BH=16 bytes/char, BL=block 0 */
        r.ecx = (u32)count;
        r.edx = (u32)first;
        r.es = fbuf.seg;
        r.ebp = 0;
        rm_int(0x10, &r);
        return;
    }
    plane2_begin();
    {
        u8 FAR *p = vga_font_ptr();
        for (c = first; c < first + count; c++)
            fmemcpy(p + c * 32, vga_font + c * 16, 16);
    }
    plane2_end();
}

/* keep_mode: started from an engine switch; the screen is already in our
 * 8-dot mode with the glyphs loaded, so do not set the mode again (no
 * window resize or flash in emulators, and the old screen stays up until
 * the first redraw) */
int vga_init(int want_font, int bios_upload, int keep_mode)
{
    vga_detect();
    if (!keep_mode)
        set_mode3();
    cursor_hide(1);
    if (!vga_present)
        return 0;
    blink_off();
    read_rom_font();
    if (!want_font)
        return 0;
    vga_biosfont = bios_upload;
    if (dos_alloc(&fbuf, 256 * 16))
        vga_biosfont = 0;
    glyphs_build(vga_font);
    vga_fontmode = 1;
    if (vga_biosfont)
        set_8dot();                 /* 1110h may reprogram; do it after */
    vga_upload(G_FIRST, G_COUNT);
    set_8dot();
    return 1;
}

/* keep_mode: exiting for an engine switch; leave the mode, font and
 * screen for the other engine */
void vga_exit(int keep_mode)
{
    if (!keep_mode) {
        set_mode3();                /* restores ROM font and 9-dot clock */
        cursor_hide(0);
    }
    vga_fontmode = 0;
    if (fbuf.seg)
        dos_free(&fbuf);
}
