/*
 * plat.h - hides the differences between the 16-bit real-mode build
 * (CDPL16, wcc -ml) and the 32-bit DPMI build (CDPL32, wcc386 -mf).
 *
 * Everything that talks to DOS, BIOS or MSCDEX with a segment register
 * goes through rm_int(): int86/intr() in real mode, DPMI 0300h (simulate
 * real-mode interrupt) in protected mode.  Buffers handed to real-mode
 * code come from dos_alloc(), which gives both the real-mode segment
 * and a pointer this program can use.
 */
#ifndef PLAT_H
#define PLAT_H

#include <string.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef signed short   s16;
typedef signed long    s32;

#ifdef __386__
#define CDPL32 1
#define FAR
#define fmemcpy  memcpy
#define fmemset  memset
#define fmemcmp  memcmp
#define ENGINE_BITS 32
#else
#define CDPL16 1
#define FAR __far
#define fmemcpy  _fmemcpy
#define fmemset  _fmemset
#define fmemcmp  _fmemcmp
#define ENGINE_BITS 16
#endif

/* Register image in DPMI 0300h layout; used by both builds. */
#pragma pack(push, 1)
typedef struct {
    u32 edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
} rmregs_t;
#pragma pack(pop)

#define RM_CF 0x0001

/* Low-memory block, addressable as seg:0 from real mode. */
typedef struct {
    u16      seg;       /* real-mode segment, 0 if not allocated */
    u16      sel;       /* DPMI selector (32-bit only) */
    u8 FAR  *p;         /* pointer usable by this program */
    u32      size;
} dosbuf_t;

/* Initialise the platform layer; returns 0 on success. */
int   plat_init(void);
void  plat_exit(void);
const char *plat_name(void);        /* "16-bit real mode", "32-bit DPMI" */

/* Simulate a real-mode software interrupt.  Returns 1 if CF was set. */
int   rm_int(int intno, rmregs_t *r);
void  rm_clear(rmregs_t *r);

int   dos_alloc(dosbuf_t *b, u32 bytes);
void  dos_free(dosbuf_t *b);

/* DMA-able memory: below 1 MB, physically contiguous, does not cross a
 * 64K physical boundary.  'phys' is the physical address. */
typedef struct {
    dosbuf_t  raw;      /* the allocation */
    u8 FAR   *p;        /* start of the usable window */
    u32       phys;
    u16       seg;      /* real-mode segment of the window (offset 0) */
    u32       size;
} dmabuf_t;
int   dma_alloc(dmabuf_t *d, u32 bytes);
void  dma_free(dmabuf_t *d);

/* Hardware IRQ hook (0..15).  The handler is an __interrupt routine. */
typedef void (__interrupt FAR *isr_t)(void);
int   irq_hook(int irq, isr_t isr);
void  irq_unhook(void);
void  irq_eoi(int irq);

/* Pointers into the first megabyte */
u16 FAR *video_ptr(void);           /* B800:0000 */
u8  FAR *vga_font_ptr(void);        /* A000:0000 */
u8  FAR *lowmem_ptr(u16 seg, u16 off);
u32   bios_ticks(void);             /* 18.2 Hz tick count */
#define TICKS_PER_SEC 18

/* Lock memory for interrupt-time use (no-op in real mode). */
void  plat_lock(void *addr, u32 len);

#endif
