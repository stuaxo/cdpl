/* sb.c - Sound Blaster 2.0 / Pro / 16 output with auto-init DMA */
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <conio.h>
#include <i86.h>
#include "sb.h"

sbcard_t sb;
volatile u16 sb_irq_count;

static int  cur_dma = -1;
static int  cur_16;
static u32  cur_size;

/* ---- DSP ------------------------------------------------------------- */

void sb_write(u8 v)
{
    unsigned n = 0xFFFF;
    while ((inp(sb.base + 0xC) & 0x80) && --n)
        ;
    outp(sb.base + 0xC, v);
}

int sb_read(void)
{
    unsigned n = 0xFFFF;
    while (!(inp(sb.base + 0xE) & 0x80))
        if (!--n)
            return -1;
    return inp(sb.base + 0xA);
}

int sb_reset_dsp(void)
{
    int i;

    outp(sb.base + 6, 1);
    for (i = 0; i < 32; i++)
        inp(sb.base + 6);               /* > 3 us */
    outp(sb.base + 6, 0);
    for (i = 0; i < 1000; i++)
        if (sb_read() == 0xAA)
            return 0;
    return -1;
}

void sb_mixer(u8 reg, u8 val)
{
    outp(sb.base + 4, reg);
    outp(sb.base + 5, val);
}

u8 sb_mixer_read(u8 reg)
{
    outp(sb.base + 4, reg);
    return (u8)inp(sb.base + 5);
}

static long hexval(const char *s, int base)
{
    return strtol(s, 0, base);
}

int sb_detect(void)
{
    const char *e = getenv("BLASTER");
    const char *p;
    int hi, lo;

    memset(&sb, 0, sizeof(sb));
    sb.irq = -1; sb.dma8 = -1; sb.dma16 = -1;
    if (!e) {
        strcpy(sb.desc, "no BLASTER variable");
        return -1;
    }
    for (p = e; *p; p++) {
        int c = toupper((u8)*p);
        if (p != e && p[-1] != ' ')
            continue;
        switch (c) {
        case 'A': sb.base = (u16)hexval(p + 1, 16); break;
        case 'I': sb.irq = (int)hexval(p + 1, 10); break;
        case 'D': sb.dma8 = (int)hexval(p + 1, 10); break;
        case 'H': sb.dma16 = (int)hexval(p + 1, 10); break;
        }
    }
    if (!sb.base || sb.irq < 0 || sb.dma8 < 0) {
        strcpy(sb.desc, "BLASTER incomplete");
        return -1;
    }
    if (sb_reset_dsp()) {
        sprintf(sb.desc, "no DSP at %Xh", sb.base);
        return -1;
    }
    sb_write(0xE1);
    hi = sb_read();
    lo = sb_read();
    if (hi < 0) {
        strcpy(sb.desc, "DSP version read failed");
        return -1;
    }
    sb.dsp_major = (u8)hi;
    sb.dsp_minor = (u8)(lo < 0 ? 0 : lo);
    if (hi >= 4) {
        sb.type = SB_16;
        sb.rate = 44100;
        sb.stereo = 1;
        sb.bits16 = sb.dma16 >= 4;
    } else if (hi == 3) {
        sb.type = SB_PRO;
        sb.rate = 22050;
        sb.stereo = 1;
        sb.highspeed = 1;
    } else if (hi == 2) {
        sb.type = SB_20;
        sb.rate = 22050;
    } else {
        sprintf(sb.desc, "DSP %d.%02d unsupported", hi, lo);
        return -1;
    }
    sprintf(sb.desc, "%s DSP %d.%02d  %u Hz %d-bit %s",
            sb.type == SB_16 ? "SB16" : sb.type == SB_PRO ? "SB Pro" : "SB 2.0",
            hi, lo, sb.rate, sb.bits16 ? 16 : 8, sb.stereo ? "stereo" : "mono");
    sb.present = 1;
    return 0;
}

/* ---- ISA DMA ---------------------------------------------------------- */

static const u8 page8[4]  = { 0x87, 0x83, 0x81, 0x82 };
static const u8 page16[4] = { 0x8F, 0x8B, 0x89, 0x8A };

static void dma_program(int ch, u32 phys, u32 size)
{
    if (ch < 4) {
        u16 cnt = (u16)(size - 1);
        outp(0x0A, 4 | ch);                     /* mask */
        outp(0x0C, 0);                          /* clear flip-flop */
        outp(0x0B, 0x58 | ch);                  /* single, auto-init, read */
        outp(ch * 2, (u8)phys);
        outp(ch * 2, (u8)(phys >> 8));
        outp(page8[ch], (u8)(phys >> 16));
        outp(ch * 2 + 1, (u8)cnt);
        outp(ch * 2 + 1, (u8)(cnt >> 8));
        outp(0x0A, ch);                         /* unmask */
    } else {
        int c = ch - 4;
        u16 addr = (u16)((phys >> 1) & 0xFFFF);
        u16 cnt = (u16)(size / 2 - 1);
        outp(0xD4, 4 | c);
        outp(0xD8, 0);
        outp(0xD6, 0x58 | c);
        outp(0xC0 + c * 4, (u8)addr);
        outp(0xC0 + c * 4, (u8)(addr >> 8));
        outp(page16[c], (u8)((phys >> 16) & 0xFE));
        outp(0xC2 + c * 4, (u8)cnt);
        outp(0xC2 + c * 4, (u8)(cnt >> 8));
        outp(0xD4, c);
    }
}

static void dma_mask(int ch)
{
    if (ch < 0)
        return;
    if (ch < 4)
        outp(0x0A, 4 | ch);
    else
        outp(0xD4, 4 | (ch - 4));
}

u32 sb_dma_remaining(void)
{
    u16 lo, hi, cnt;

    if (cur_dma < 0)
        return 0;
    _disable();
    if (cur_dma < 4) {
        outp(0x0C, 0);
        lo = (u16)inp(cur_dma * 2 + 1);
        hi = (u16)inp(cur_dma * 2 + 1);
    } else {
        outp(0xD8, 0);
        lo = (u16)inp(0xC2 + (cur_dma - 4) * 4);
        hi = (u16)inp(0xC2 + (cur_dma - 4) * 4);
    }
    _enable();
    cnt = (u16)(lo | (hi << 8));
    cnt++;                               /* 0xFFFF after terminal count */
    return cur_dma < 4 ? (u32)cnt : (u32)cnt * 2;
}

/* ---- interrupt ------------------------------------------------------- */

static void __interrupt FAR sb_isr(void)
{
    if (cur_16)
        inp(sb.base + 0xF);
    else
        inp(sb.base + 0xE);
    sb_irq_count++;
    irq_eoi(sb.irq);
}

/* ---- playback -------------------------------------------------------- */

int sb_start(dmabuf_t *buf, u32 size)
{
    u32 half = size / 2;

    if (!sb.present)
        return -1;
    sb_reset_dsp();
    sb_irq_count = 0;
    cur_16 = sb.bits16;
    cur_dma = cur_16 ? sb.dma16 : sb.dma8;
    cur_size = size;
    if (irq_hook(sb.irq, sb_isr))
        return -1;
    dma_program(cur_dma, buf->phys, size);

    sb_write(0xD1);                      /* speaker on */
    if (sb.type == SB_16) {
        sb_write(0x41);
        sb_write((u8)(sb.rate >> 8));
        sb_write((u8)sb.rate);
        if (cur_16) {
            u16 n = (u16)(half / 2 - 1);
            sb_write(0xB6);              /* 16-bit, auto-init, FIFO */
            sb_write(0x30);              /* stereo, signed */
            sb_write((u8)n);
            sb_write((u8)(n >> 8));
        } else {
            u16 n = (u16)(half - 1);
            sb_write(0xC6);              /* 8-bit, auto-init, FIFO */
            sb_write(sb.stereo ? 0x20 : 0x00);
            sb_write((u8)n);
            sb_write((u8)(n >> 8));
        }
    } else {
        int ch = sb.stereo ? 2 : 1;
        u8 tc = (u8)(256 - 1000000L / ((long)ch * sb.rate));
        u16 n = (u16)(half - 1);
        if (sb.type == SB_PRO)
            sb_mixer(0x0E, (u8)((sb_mixer_read(0x0E) & ~0x02) |
                                (sb.stereo ? 0x22 : 0x20)));
        sb_write(0x40);
        sb_write(tc);
        sb_write(0x48);
        sb_write((u8)n);
        sb_write((u8)(n >> 8));
        sb_write(sb.highspeed ? 0x90 : 0x1C);
    }
    return 0;
}

void sb_stop(void)
{
    if (cur_dma < 0)
        return;
    if (!sb.highspeed) {
        sb_write(cur_16 ? 0xD5 : 0xD0);
        sb_write(cur_16 ? 0xD9 : 0xDA);
    }
    sb_reset_dsp();
    dma_mask(cur_dma);
    irq_unhook();
    if (sb.type == SB_PRO)
        sb_mixer(0x0E, (u8)(sb_mixer_read(0x0E) & ~0x02));
    cur_dma = -1;
}

void sb_pause(void)
{
    if (cur_dma >= 0 && !sb.highspeed)
        sb_write(cur_16 ? 0xD5 : 0xD0);
}

void sb_continue(void)
{
    if (cur_dma >= 0 && !sb.highspeed)
        sb_write(cur_16 ? 0xD6 : 0xD4);
}

void sb_set_voice_volume(u8 l, u8 r)
{
    if (!sb.present)
        return;
    if (sb.type == SB_16) {
        sb_mixer(0x32, (u8)(l & 0xF8));
        sb_mixer(0x33, (u8)(r & 0xF8));
    } else if (sb.type == SB_PRO) {
        sb_mixer(0x04, (u8)((l & 0xF0) | (r >> 4)));
    } else {
        sb_mixer(0x0A, (u8)(l >> 5));    /* CT1335: voice, 2 bits */
    }
}

void sb_set_cd_volume(u8 l, u8 r)
{
    if (!sb.present)
        return;
    if (sb.type == SB_16) {
        sb_mixer(0x36, (u8)(l & 0xF8));
        sb_mixer(0x37, (u8)(r & 0xF8));
    } else if (sb.type == SB_PRO) {
        sb_mixer(0x28, (u8)((l & 0xF0) | (r >> 4)));
    } else {
        sb_mixer(0x08, (u8)(l >> 4));
    }
}
