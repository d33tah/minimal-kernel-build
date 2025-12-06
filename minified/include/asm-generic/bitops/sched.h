/* i386 - BITS_PER_LONG == 32 */
#ifndef _ASM_GENERIC_BITOPS_SCHED_H_
#define _ASM_GENERIC_BITOPS_SCHED_H_

#include <linux/compiler.h>
#include <asm/types.h>

static inline int sched_find_first_bit(const unsigned long *b)
{
	if (b[0])
		return __ffs(b[0]);
	if (b[1])
		return __ffs(b[1]) + 32;
	if (b[2])
		return __ffs(b[2]) + 64;
	return __ffs(b[3]) + 96;
}

#endif
