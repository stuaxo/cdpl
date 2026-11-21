/* cpu.h - is the CPU a 386 or later (can the 32-bit engine run)? */
#ifndef CPU_H
#define CPU_H

#ifdef __386__
#define cpu_is_386() 1              /* we are running 32-bit code */
#else
int cpu_is_386(void);               /* 0 on an 8086/8088/186/286 */
#endif

#endif
