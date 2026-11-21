/* mouse.c - pointer position from mickeys (fn 0Bh) at pixel precision,
 * or from the driver position (fn 3) with /MABS */
#include "plat.h"
#include "mouse.h"

int mouse_present;
int mouse_x = 320, mouse_y = 200;
int mouse_buttons;
int mouse_absolute;

static int driver_cursor;
static int frac_y;

static void m33(rmregs_t *r) { rm_int(0x33, r); }

/* move the pointer; the driver position is kept in step for /MABS and
 * the driver's own cursor (8 units per text row there) */
void mouse_setpos(int x, int y)
{
    rmregs_t r;

    if (x < 0 || x > 639 || y < 0 || y > 399)
        return;
    mouse_x = x;
    mouse_y = y;
    if (!mouse_present)
        return;
    rm_clear(&r);
    r.eax = 4;
    r.ecx = (u32)x;
    r.edx = (u32)(driver_cursor ? y / 2 : y);
    m33(&r);
}

int mouse_init(int show_driver_cursor)
{
    rmregs_t r;

    rm_clear(&r);
    r.eax = 0;
    m33(&r);
    mouse_present = (u16)r.eax == 0xFFFF;
    if (!mouse_present)
        return 0;
    driver_cursor = show_driver_cursor;
    /* range 0-639 x 0-399 so fn 3 gives pixels in 80x25; the driver's
     * own text cursor needs its usual 8 units per row */
    rm_clear(&r); r.eax = 7; r.ecx = 0; r.edx = 639; m33(&r);
    rm_clear(&r); r.eax = 8; r.ecx = 0; r.edx = driver_cursor ? 199 : 399; m33(&r);
    /* 8 mickeys per 8 units both ways (default is 16 vertically), so one
     * mickey is one unit of the driver's own position and the two stay
     * in step; see the resync in mouse_poll */
    rm_clear(&r); r.eax = 0x0F; r.ecx = 8; r.edx = driver_cursor ? 16 : 8; m33(&r);
    mouse_setpos(mouse_x, mouse_y);
    rm_clear(&r); r.eax = 0x0B; m33(&r);       /* clear motion counters */
    if (driver_cursor) {
        rm_clear(&r); r.eax = 1; m33(&r);
    }
    return 1;
}

/* before the video mode is restored: take the driver cursor down if we
 * showed it, so the mode set cannot leave it drawn */
void mouse_hide(void)
{
    rmregs_t r;

    if (!mouse_present || !driver_cursor)
        return;
    rm_clear(&r);
    r.eax = 2;
    m33(&r);
    driver_cursor = 0;
}

/* after the video mode is restored: reset so the driver picks up the
 * restored mode and its default ranges, with the cursor hidden (hide
 * count -1), which is what the next program (or the other engine)
 * expects from a freshly reset driver */
void mouse_exit(void)
{
    rmregs_t r;

    if (!mouse_present)
        return;
    mouse_hide();
    rm_clear(&r);
    r.eax = 0;
    m33(&r);
}

int mouse_poll(void)
{
    rmregs_t r;
    int ox = mouse_x, oy = mouse_y, ob = mouse_buttons, dx, dy;

    if (!mouse_present)
        return 0;
    rm_clear(&r);
    r.eax = 3;
    m33(&r);
    mouse_buttons = (int)(r.ebx & 3);
    dx = (int)(s16)r.ecx;
    dy = (int)(s16)r.edx * (driver_cursor ? 2 : 1);
    if (mouse_absolute || driver_cursor) {
        mouse_x = dx;
        mouse_y = dy;
    } else {
        rm_clear(&r);
        r.eax = 0x0B;
        m33(&r);
        /* ratio set to 8 per 8 both ways in mouse_init: one mickey is
         * one pixel of our 640x400 */
        mouse_x += (s16)r.ecx;
        frac_y += (s16)r.edx;
        mouse_y += frac_y;
        frac_y = 0;
        /* the driver's position moves with the same mickeys, so ours
         * stays within a cell of it (a real driver rounds to cells in text
         * mode).  If it is further off, the driver was moved behind our
         * back: dosemu2 and other emulators re-sync it to the host pointer
         * after a reset or range change and report that jump as mickeys.
         * Follow the driver then. */
        if (dx - mouse_x >= 8 || mouse_x - dx >= 8 ||
            dy - mouse_y >= 16 || mouse_y - dy >= 16) {
            mouse_x = dx;
            mouse_y = dy;
        }
    }
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > 639) mouse_x = 639;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > 399) mouse_y = 399;
    return ox != mouse_x || oy != mouse_y || ob != mouse_buttons;
}
