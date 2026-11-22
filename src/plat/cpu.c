/* cpu.c - 8086 / 286 / 386+ detection for 16-bit code (CDPL16, CDPL.EXE)
 *
 * The FLAGS test: an 8086/186 keeps bits 12-15 set, a 286 in real mode
 * keeps bits 12-14 clear, a 386 lets them change.  In V86 mode (EMM386,
 * a DOS box, an emulator) POPF may not change IOPL, so a "286" answer is
 * checked with SMSW: the PE bit set while DOS runs means V86 mode, which
 * only a 386 or later has. */
#include "cpu.h"

/* set FLAGS to f, read them back, restore the originals */
static unsigned short flags_try(unsigned short f);
#pragma aux flags_try = \
    "pushf"  \
    "push ax" \
    "popf"   \
    "pushf"  \
    "pop ax" \
    "popf"   \
    parm [ax] value [ax];

static unsigned short flags_get(void);
#pragma aux flags_get = \
    "pushf"  \
    "pop ax" \
    value [ax];

/* SMSW AX, as bytes so it assembles in 8086 code (only run on a 286+) */
static unsigned short msw_get(void);
#pragma aux msw_get = \
    "db 0Fh, 01h, 0E0h" \
    value [ax];

int cpu_level(void)
{
    static int level = -1;
    unsigned short f;

    if (level >= 0)
        return level;
    f = (unsigned short)(flags_get() & 0x0FFF);
    if ((flags_try(f) & 0xF000) == 0xF000)
        level = CPU_8086;
    else if (flags_try((unsigned short)(f | 0x7000)) & 0x7000)
        level = CPU_386;
    else                                /* 286 real mode, or 386+ in V86 */
        level = (msw_get() & 1) ? CPU_386 : CPU_286;
    return level;
}
