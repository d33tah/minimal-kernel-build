#ifndef __LINUX_BITMAP_H
#define __LINUX_BITMAP_H

#include <linux/kernel.h>

/*
 * Every bitmap searched here is a single word (the fd table's open_fds,
 * NR_OPEN_DEFAULT == BITS_PER_LONG; nodemasks, MAX_NUMNODES == 1), so the
 * search helpers handle 0 < size <= BITS_PER_LONG only.
 */
static inline unsigned long find_next_zero_bit(const unsigned long *addr,
					       unsigned long size,
					       unsigned long offset)
{
	unsigned long val;

	if (unlikely(offset >= size))
		return size;

	val = *addr | ~GENMASK(size - 1, offset);
	return val == ~0UL ? size : ffz(val);
}

#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (BITS_PER_LONG - 1)))

static inline bool bitmap_empty(const unsigned long *src, unsigned nbits)
{
	return !(*src & BITMAP_LAST_WORD_MASK(nbits));
}

#endif
