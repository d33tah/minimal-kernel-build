/* Hash functions for i386 (BITS_PER_LONG == 32) */
#ifndef _LINUX_HASH_H
#define _LINUX_HASH_H

#define GOLDEN_RATIO_32 0x61C88647

#define __hash_32 __hash_32_generic
static inline u32 __hash_32_generic(u32 val)
{
	return val * GOLDEN_RATIO_32;
}

#endif
