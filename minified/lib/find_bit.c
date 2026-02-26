
#include <linux/bitops.h>
#include <linux/bitmap.h>
#include <linux/math.h>
#include <linux/minmax.h>

/* All callers pass le=0, so LE support removed */
unsigned long _find_next_bit(const unsigned long *addr1,
			     const unsigned long *addr2, unsigned long nbits,
			     unsigned long start, unsigned long invert,
			     unsigned long le)
{
	unsigned long tmp, mask;

	if (unlikely(start >= nbits))
		return nbits;

	tmp = addr1[start / BITS_PER_LONG];
	if (addr2)
		tmp &= addr2[start / BITS_PER_LONG];
	tmp ^= invert;

	tmp &= BITMAP_FIRST_WORD_MASK(start);

	start = round_down(start, BITS_PER_LONG);

	while (!tmp) {
		start += BITS_PER_LONG;
		if (start >= nbits)
			return nbits;

		tmp = addr1[start / BITS_PER_LONG];
		if (addr2)
			tmp &= addr2[start / BITS_PER_LONG];
		tmp ^= invert;
	}

	return min(start + __ffs(tmp), nbits);
}

#ifndef find_first_bit
unsigned long _find_first_bit(const unsigned long *addr, unsigned long size)
{
	unsigned long idx;

	for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
		if (addr[idx])
			return min(idx * BITS_PER_LONG + __ffs(addr[idx]),
				   size);
	}

	return size;
}
#endif

#ifndef find_first_zero_bit
unsigned long _find_first_zero_bit(const unsigned long *addr,
				   unsigned long size)
{
	unsigned long idx;

	for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
		if (addr[idx] != ~0UL)
			return min(idx * BITS_PER_LONG + ffz(addr[idx]), size);
	}

	return size;
}
#endif
