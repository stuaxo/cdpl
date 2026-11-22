/* mouse.h - INT 33h with our own pixel position */
#ifndef MOUSE_H
#define MOUSE_H

extern int mouse_present;
extern int mouse_x, mouse_y;      /* pixels, 0-639 x 0-399 */
extern int mouse_buttons;         /* bit 0 left, bit 1 right */
extern int mouse_absolute;        /* use the driver position (fn 3) */

int  mouse_init(int show_driver_cursor);
void mouse_hide(void);            /* before a mode set on exit */
void mouse_exit(void);            /* after it: reset, cursor hidden */
int  mouse_poll(void);
void mouse_setpos(int x, int y);            /* returns 1 if anything changed */

#endif
