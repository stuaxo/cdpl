/* vga.h - text mode, 8-dot characters and font access */
#ifndef VGA_H
#define VGA_H
#include "plat.h"

extern int vga_present;
extern int vga_fontmode;            /* custom glyphs loaded */
extern int vga_biosfont;            /* upload with INT 10h 1110h, not plane 2 */
extern u8  vga_font[256 * 16];      /* current font image */

int   vga_detect(void);
int   vga_init(int want_font, int bios_upload, int keep_mode);
void  vga_exit(int keep_mode);
void  vga_upload(int first, int count);     /* from vga_font[] */
void  vga_wait_retrace(void);

#endif
