/* cpu.h - CPU class: can the 32-bit engine run, is it a slow 8086? */
#ifndef CPU_H
#define CPU_H

#define CPU_8086 0                  /* 8086/8088/V20/186 */
#define CPU_286  2
#define CPU_386  3                  /* 386 or later */

#ifdef __386__
#define cpu_level() CPU_386         /* we are running 32-bit code */
#else
int cpu_level(void);
#endif
#define cpu_is_386() (cpu_level() >= CPU_386)

#endif
