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

#ifndef MAX_UDELAY_MS
#define MAX_UDELAY_MS	5
#endif

#ifndef mdelay
#define mdelay(n) (\
	(__builtin_constant_p(n) && (n)<=MAX_UDELAY_MS) ? udelay((n)*1000) : \
	({unsigned long __ms=(n); while (__ms--) udelay(1000);}))
#endif

void calibrate_delay(void);

#endif
