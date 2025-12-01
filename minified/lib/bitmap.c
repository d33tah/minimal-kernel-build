
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


bool __bitmap_equal(const unsigned long *bitmap1,
		    const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] != bitmap2[k])
			return false;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] ^ bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return false;

	return true;
}

/* Stub: __bitmap_or_equal not called externally */
bool __bitmap_or_equal(const unsigned long *bitmap1,
		       const unsigned long *bitmap2,
		       const unsigned long *bitmap3,
		       unsigned int bits)
{
	return false;
}

/* Stub: bitmap_cut not used in minimal kernel */
void bitmap_cut(unsigned long *dst, const unsigned long *src,
		unsigned int first, unsigned int cut, unsigned int nbits)
{
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

/* Stub: __bitmap_xor not used in minimal kernel */
void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, unsigned int bits)
{
}

/* Stub: __bitmap_andnot not used in minimal kernel */
int __bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, unsigned int bits)
{
	return 0;
}

/* Stub: __bitmap_replace not called externally */
void __bitmap_replace(unsigned long *dst,
		      const unsigned long *old, const unsigned long *new,
		      const unsigned long *mask, unsigned int nbits)
{
}

/* Stub: __bitmap_intersects not used in minimal kernel */
bool __bitmap_intersects(const unsigned long *bitmap1,
			 const unsigned long *bitmap2, unsigned int bits)
{
	return false;
}

/* Stub: __bitmap_subset not used in minimal kernel */
bool __bitmap_subset(const unsigned long *bitmap1,
		     const unsigned long *bitmap2, unsigned int bits)
{
	return true;
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


/* Stubbed bitmap parsing functions - not used */
int bitmap_parse_user(const char __user *ubuf,
			unsigned int ulen, unsigned long *maskp,
			int nmaskbits)
{
	return -EINVAL;
}

int bitmap_print_to_pagebuf(bool list, char *buf, const unsigned long *maskp,
			    int nmaskbits)
{
	 
	return 0;
}

int bitmap_print_bitmask_to_buf(char *buf, const unsigned long *maskp,
				 int nmaskbits, loff_t off, size_t count)
{
	 
	return 0;
}

int bitmap_print_list_to_buf(char *buf, const unsigned long *maskp,
			      int nmaskbits, loff_t off, size_t count)
{
	 
	return 0;
}

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

/* Stubbed remap, region, and allocation functions - not used */
unsigned int bitmap_ord_to_pos(const unsigned long *buf, unsigned int ord, unsigned int nbits)
{
	return 0;
}

void bitmap_remap(unsigned long *dst, const unsigned long *src,
		const unsigned long *old, const unsigned long *new,
		unsigned int nbits)
{
}

int bitmap_bitremap(int oldbit, const unsigned long *old,
				const unsigned long *new, int bits)
{
	return oldbit;
}

int bitmap_find_free_region(unsigned long *bitmap, unsigned int bits, int order)
{
	return -ENOMEM;
}

void bitmap_release_region(unsigned long *bitmap, unsigned int pos, int order)
{
}

int bitmap_allocate_region(unsigned long *bitmap, unsigned int pos, int order)
{
	return -EBUSY;
}

unsigned long *bitmap_alloc(unsigned int nbits, gfp_t flags)
{
	return NULL;
}

unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags)
{
	return NULL;
}

unsigned long *bitmap_alloc_node(unsigned int nbits, gfp_t flags, int node)
{
	return NULL;
}

unsigned long *bitmap_zalloc_node(unsigned int nbits, gfp_t flags, int node)
{
	return NULL;
}

void bitmap_free(const unsigned long *bitmap)
{
}

unsigned long *devm_bitmap_alloc(struct device *dev,
				 unsigned int nbits, gfp_t flags)
{
	return NULL;
}

unsigned long *devm_bitmap_zalloc(struct device *dev,
				  unsigned int nbits, gfp_t flags)
{
	return devm_bitmap_alloc(dev, nbits, flags | __GFP_ZERO);
}

#if BITS_PER_LONG == 64
/* Stub: bitmap_from_arr32 not used in minimal kernel */
void bitmap_from_arr32(unsigned long *bitmap, const u32 *buf, unsigned int nbits)
{
}

/* Stub: bitmap_to_arr32 not used in minimal kernel */
void bitmap_to_arr32(u32 *buf, const unsigned long *bitmap, unsigned int nbits)
{
}
#endif

#if (BITS_PER_LONG == 32) && defined(__BIG_ENDIAN)
void bitmap_from_arr64(unsigned long *bitmap, const u64 *buf, unsigned int nbits)
{
	int n;

	for (n = nbits; n > 0; n -= 64) {
		u64 val = *buf++;

		*bitmap++ = val;
		if (n > 32)
			*bitmap++ = val >> 32;
	}

	 
	if (nbits % BITS_PER_LONG)
		bitmap[-1] &= BITMAP_LAST_WORD_MASK(nbits);
}

void bitmap_to_arr64(u64 *buf, const unsigned long *bitmap, unsigned int nbits)
{
	const unsigned long *end = bitmap + BITS_TO_LONGS(nbits);

	while (bitmap < end) {
		*buf = *bitmap++;
		if (bitmap < end)
			*buf |= (u64)(*bitmap++) << 32;
		buf++;
	}

	 
	if (nbits % 64)
		buf[-1] &= GENMASK_ULL(nbits % 64, 0);
}
#endif
