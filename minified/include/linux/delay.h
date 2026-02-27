#ifndef _LINUX_DELAY_H
#define _LINUX_DELAY_H

#include <linux/math.h>

extern unsigned long loops_per_jiffy;

/* asm/delay.h inlined */
extern void __bad_udelay(void);
extern void __udelay(unsigned long usecs);
extern void __const_udelay(unsigned long xloops);
extern void __delay(unsigned long loops);
#define udelay(n)							\
	({								\
		if (__builtin_constant_p(n)) {				\
			if ((n) / 20000 >= 1)				\
				 __bad_udelay();			\
			else						\
				__const_udelay((n) * 0x10c7ul);		\
		} else {						\
			__udelay(n);					\
		}							\
	})
#include <linux/init.h>
void __init use_tsc_delay(void);

#endif
