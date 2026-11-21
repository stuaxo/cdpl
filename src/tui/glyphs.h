/* glyphs.h - character slots redefined in the VGA font (0x80-0xAB).
 * With /NOFONT, the CP437 fallbacks (G_FB_*) are used instead. */
#ifndef GLYPHS_H
#define GLYPHS_H

#define G_FIRST     0x80
#define G_CHK_OFF   0x80
#define G_CHK_ON    0x81
#define G_RAD_OFF   0x82
#define G_RAD_ON    0x83
#define G_FILL0     0x84        /* bar: 0..8 pixels filled from the left */
#define G_FILL8     0x8C
#define G_CLOSE     0x8D
#define G_ICONS     0x8E        /* 8 two-cell icons */
#define G_NOTE      0x9E
#define G_DISC      0x9F
#define G_PTR_A     0xA0        /* 3x2 cells, pointer buffer A */
#define G_PTR_B     0xA6        /* 3x2 cells, pointer buffer B */
#define G_LAST      0xAB
#define G_COUNT     (G_LAST - G_FIRST + 1)

enum { IC_PREV, IC_REW, IC_PLAY, IC_PAUSE, IC_STOP, IC_FFWD, IC_NEXT, IC_EJECT };
#define G_ICON(i)   (G_ICONS + (i) * 2)

/* build the custom glyphs (16 bytes each) into font[], which holds all
 * 256 characters at 16 bytes per character */
void glyphs_build(unsigned char *font);

/* mouse arrow: 16 rows, bit 15 = leftmost pixel */
extern unsigned short ptr_mask[16];   /* pixels drawn as background */
extern unsigned short ptr_image[16];  /* pixels drawn as foreground */

#endif
