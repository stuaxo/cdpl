/* plat16.c - real-mode (16-bit, large model) platform layer */
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include "plat.h"

static int   hooked_irq = -1;
static void (__interrupt __far *old_vec)(void);

int plat_init(void) { return 0; }
void plat_exit(void) { irq_unhook(); }
const char *plat_name(void) { return "16-bit real mode"; }

void rm_clear(rmregs_t *r) { memset(r, 0, sizeof(*r)); }

int rm_int(int intno, rmregs_t *r)
{
    union REGPACK p;

    memset(&p, 0, sizeof(p));
    p.w.ax = (u16)r->eax;
    p.w.bx = (u16)r->ebx;
    p.w.cx = (u16)r->ecx;
    p.w.dx = (u16)r->edx;
    p.w.si = (u16)r->esi;
    p.w.di = (u16)r->edi;
    p.w.bp = (u16)r->ebp;
    p.w.ds = r->ds;
    p.w.es = r->es;
    p.w.flags = r->flags;
    intr(intno, &p);
    r->eax = p.w.ax;
    r->ebx = p.w.bx;
    r->ecx = p.w.cx;
    r->edx = p.w.dx;
    r->esi = p.w.si;
    r->edi = p.w.di;
    r->ebp = p.w.bp;
    r->ds = p.w.ds;
    r->es = p.w.es;
    r->flags = p.w.flags;
    return (p.w.flags & RM_CF) != 0;
}

int dos_alloc(dosbuf_t *b, u32 bytes)
{
    unsigned seg;
    unsigned paras = (unsigned)((bytes + 15) >> 4);

    memset(b, 0, sizeof(*b));
    if (bytes > 0xFFFF0UL || _dos_allocmem(paras, &seg) != 0)
        return -1;
    b->seg = seg;
    b->p = (u8 __far *)MK_FP(seg, 0);
    b->size = bytes;
    return 0;
}

void dos_free(dosbuf_t *b)
{
    if (b->seg)
        _dos_freemem(b->seg);
    b->seg = 0;
    b->p = 0;
}

int dma_alloc(dmabuf_t *d, u32 bytes)
{
    u32 phys, next;

    memset(d, 0, sizeof(*d));
    if (bytes > 0xFFF0UL || dos_alloc(&d->raw, bytes * 2 + 16))
        return -1;
    phys = (u32)d->raw.seg << 4;
    next = (phys + 0xFFFFUL) & 0xFFFF0000UL;
    if (next != phys && phys + bytes > next)
        phys = next;                    /* would cross: start at boundary */
    d->phys = phys;
    d->seg = (u16)(phys >> 4);
    d->p = (u8 __far *)MK_FP(d->seg, 0);
    d->size = bytes;
    return 0;
}

void dma_free(dmabuf_t *d) { dos_free(&d->raw); d->p = 0; }

static int irq_vec(int irq) { return irq < 8 ? 8 + irq : 0x70 + irq - 8; }

int irq_hook(int irq, isr_t isr)
{
    u8 m;

    irq_unhook();
    old_vec = _dos_getvect(irq_vec(irq));
    _dos_setvect(irq_vec(irq), isr);
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
    _dos_setvect(irq_vec(hooked_irq), old_vec);
    hooked_irq = -1;
}

void irq_eoi(int irq)
{
    if (irq >= 8)
        outp(0xA0, 0x20);
    outp(0x20, 0x20);
}

u16 __far *video_ptr(void) { return (u16 __far *)MK_FP(0xB800, 0); }
u8 __far *vga_font_ptr(void) { return (u8 __far *)MK_FP(0xA000, 0); }
u8 __far *lowmem_ptr(u16 seg, u16 off) { return (u8 __far *)MK_FP(seg, off); }
u32 bios_ticks(void) { return *(volatile u32 __far *)MK_FP(0x40, 0x6C); }
void plat_lock(void *addr, u32 len) { (void)addr; (void)len; }
