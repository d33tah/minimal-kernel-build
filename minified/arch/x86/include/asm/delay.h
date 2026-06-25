
#ifndef _ASM_X86_DELAY_H
#define _ASM_X86_DELAY_H

extern void __bad_udelay(void);
extern void __const_udelay(unsigned long xloops);
extern void __delay(unsigned long loops);

/*
 * Every udelay() callsite in this kernel passes a compile-time constant, so
 * __builtin_constant_p(n) is always true and the out-of-line __udelay(n) arm
 * (for non-constant n) is never instantiated -- it has been removed along with
 * the __udelay() function.
 */
#define udelay(n)							\
	({								\
		if ((n) / 20000 >= 1)					\
			__bad_udelay();					\
		else							\
			__const_udelay((n) * 0x10c7ul);			\
	})

#include <linux/init.h>

void __init use_tsc_delay(void);
void __init use_tpause_delay(void);

#endif  
