/* info.h - drive information lines shared by the dialog and /INFO */
#ifndef INFO_H
#define INFO_H
#include "plat.h"

#define INFO_W   72
#define INFO_MAX 16

/* fills up to INFO_MAX lines, returns the count */
int  info_lines(char l[][INFO_W], int max);
void msf_fmt(char *b, u32 frames, int with_frames);

#endif
