/* plat32.c - 32-bit protected-mode (DPMI client, flat model) platform */
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include "plat.h"

static int   hooked_irq = -1;
static u16   old_sel;
static u32   old_off;

int plat_init(void) { return 0; }
void plat_exit(void) { irq_unhook(); }
const char *plat_name(void) { return "32-bit DPMI"; }

void rm_clear(rmregs_t *r) { memset(r, 0, sizeof(*r)); }

/* DPMI 0300h: simulate real mode interrupt.  SS:SP = 0 asks the host to
 * supply a real-mode stack. */
int rm_int(int intno, rmregs_t *r)
{
    union REGS in, out;
    struct SREGS s;

    segread(&s);
    memset(&in, 0, sizeof(in));
    in.w.ax = 0x0300;
    in.h.bl = (u8)intno;
    in.h.bh = 0;
    in.w.cx = 0;
    s.es = s.ds;
    in.x.edi = (unsigned)r;
    r->ss = 0;
    r->sp = 0;
    r->flags &= ~RM_CF;
    int386x(0x31, &in, &out, &s);
    if (out.x.cflag)
        return 1;
    return (r->flags & RM_CF) != 0;
}

int dos_alloc(dosbuf_t *b, u32 bytes)
{
    union REGS r;

    memset(b, 0, sizeof(*b));
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0100;
    r.w.bx = (u16)((bytes + 15) >> 4);
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return -1;
    b->seg = r.w.ax;
    b->sel = r.w.dx;
    b->p = (u8 *)((u32)b->seg << 4);   /* zero-based flat model */
    b->size = bytes;
    return 0;
}

void dos_free(dosbuf_t *b)
{
    union REGS r;

    if (!b->sel)
        return;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0101;
    r.w.dx = b->sel;
    int386(0x31, &r, &r);
    b->sel = 0;
    b->seg = 0;
    b->p = 0;
}

int dma_alloc(dmabuf_t *d, u32 bytes)
{
    u32 phys, next;

    memset(d, 0, sizeof(*d));
    if (bytes > 0xFFF0UL || dos_alloc(&d->raw, bytes * 2 + 16))
        return -1;
    /* conventional memory is identity-mapped under DOS extenders */
    phys = (u32)d->raw.seg << 4;
    next = (phys + 0xFFFFUL) & 0xFFFF0000UL;
    if (next != phys && phys + bytes > next)
        phys = next;
    d->phys = phys;
    d->seg = (u16)(phys >> 4);
    d->p = (u8 *)phys;
    d->size = bytes;
    plat_lock(d->p, bytes);
    return 0;
}

void dma_free(dmabuf_t *d) { dos_free(&d->raw); d->p = 0; }

static int irq_vec(int irq) { return irq < 8 ? 8 + irq : 0x70 + irq - 8; }

int irq_hook(int irq, isr_t isr)
{
    union REGS r;
    struct SREGS s;
    u8 m;

    irq_unhook();
    segread(&s);
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0204;                    /* get PM interrupt vector */
    r.h.bl = (u8)irq_vec(irq);
    int386(0x31, &r, &r);
    old_sel = r.w.cx;
    old_off = r.x.edx;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0205;                    /* set PM interrupt vector */
    r.h.bl = (u8)irq_vec(irq);
    r.w.cx = s.cs;
    r.x.edx = (u32)isr;
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return -1;
    hooked_irq = irq;
    _disable();
    if (irq < 8) {
        m = (u8)inp(0x21); outp(0x21, m & ~(1 << irq));
    } else {
        m = (u8)inp(0xA1); outp(0xA1, m & ~(1 << (irq - 8)));
        m = (u8)inp(0x21); outp(0x21, m & ~(1 << 2));
    }
    _enable();
    return 0;
}

void irq_unhook(void)
{
    union REGS r;
    u8 m;

    if (hooked_irq < 0)
        return;
    _disable();
    if (hooked_irq < 8) {
        m = (u8)inp(0x21); outp(0x21, m | (1 << hooked_irq));
    } else {
        m = (u8)inp(0xA1); outp(0xA1, m | (1 << (hooked_irq - 8)));
    }
    _enable();
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0205;
    r.h.bl = (u8)irq_vec(hooked_irq);
    r.w.cx = old_sel;
    r.x.edx = old_off;
    int386(0x31, &r, &r);
    hooked_irq = -1;
}

void irq_eoi(int irq)
{
    if (irq >= 8)
        outp(0xA0, 0x20);
    outp(0x20, 0x20);
}

u16 *video_ptr(void) { return (u16 *)0xB8000; }
u8 *vga_font_ptr(void) { return (u8 *)0xA0000; }
u8 *lowmem_ptr(u16 seg, u16 off) { return (u8 *)(((u32)seg << 4) + off); }
u32 bios_ticks(void) { return *(volatile u32 *)0x46C; }

/* DPMI 0600h: lock linear region */
void plat_lock(void *addr, u32 len)
{
    union REGS r;
    u32 lin = (u32)addr;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0600;
    r.w.bx = (u16)(lin >> 16);
    r.w.cx = (u16)lin;
    r.w.si = (u16)(len >> 16);
    r.w.di = (u16)len;
    int386(0x31, &r, &r);
}
