
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/thread_info.h>
#include <linux/uaccess.h>

#include <asm/page.h>

#include "kstrtox.h"


/* Stub: __bitmap_equal not called externally */
bool __bitmap_equal(const unsigned long *bitmap1,
		    const unsigned long *bitmap2, unsigned int bits)
{
	return false;
}

/* Stub: __bitmap_and not used in minimal kernel */
int __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, unsigned int bits)
{
	return 0;
}

/* Stub: __bitmap_or not used in minimal kernel */
void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, unsigned int bits)
{
}

/* Stub: __bitmap_intersects not used in minimal kernel */
bool __bitmap_intersects(const unsigned long *bitmap1,
			 const unsigned long *bitmap2, unsigned int bits)
{
	return false;
}

/* Stub: __bitmap_weight not used in minimal kernel */
int __bitmap_weight(const unsigned long *bitmap, unsigned int bits)
{
	return 0;
}

void __bitmap_set(unsigned long *map, unsigned int start, int len)
{
	unsigned long *p = map + BIT_WORD(start);
	const unsigned int size = start + len;
	int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

	while (len - bits_to_set >= 0) {
		*p |= mask_to_set;
		len -= bits_to_set;
		bits_to_set = BITS_PER_LONG;
		mask_to_set = ~0UL;
		p++;
	}
	if (len) {
		mask_to_set &= BITMAP_LAST_WORD_MASK(size);
		*p |= mask_to_set;
	}
}

void __bitmap_clear(unsigned long *map, unsigned int start, int len)
{
	unsigned long *p = map + BIT_WORD(start);
	const unsigned int size = start + len;
	int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);

	while (len - bits_to_clear >= 0) {
		*p &= ~mask_to_clear;
		len -= bits_to_clear;
		bits_to_clear = BITS_PER_LONG;
		mask_to_clear = ~0UL;
		p++;
	}
	if (len) {
		mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
		*p &= ~mask_to_clear;
	}
}

unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset)
{
	unsigned long index, end, i;
again:
	index = find_next_zero_bit(map, size, start);

	 
	index = __ALIGN_MASK(index + align_offset, align_mask) - align_offset;

	end = index + nr;
	if (end > size)
		return end;
	i = find_next_bit(map, end, index);
	if (i < end) {
		start = i + 1;
		goto again;
	}
	return index;
}


int bitmap_print_to_pagebuf(bool list, char *buf, const unsigned long *maskp,
			    int nmaskbits)
{
	 
	return 0;
}

/* bitmap_print_bitmask_to_buf, bitmap_print_list_to_buf removed - not called */

int bitmap_parselist(const char *buf, unsigned long *maskp, int nmaskbits)
{
	return -EINVAL;
}

int bitmap_parselist_user(const char __user *ubuf,
			unsigned int ulen, unsigned long *maskp,
			int nmaskbits)
{
	return -EINVAL;
}

int bitmap_parse(const char *start, unsigned int buflen,
		unsigned long *maskp, int nmaskbits)
{
	return -EINVAL;
}

/* bitmap_from_arr32, bitmap_to_arr32, bitmap_from_arr64, bitmap_to_arr64 removed - unused */
