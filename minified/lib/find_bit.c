
#include <linux/bitmap.h>

#if !defined(find_next_bit) || !defined(find_next_zero_bit) ||           \
	!defined(find_next_bit_le) || !defined(find_next_zero_bit_le) || \
	!defined(find_next_and_bit)
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

	mask = BITMAP_FIRST_WORD_MASK(start);
	if (le)
		mask = swab(mask);

	tmp &= mask;

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

	if (le)
		tmp = swab(tmp);

	return min(start + __ffs(tmp), nbits);
}
#endif

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
