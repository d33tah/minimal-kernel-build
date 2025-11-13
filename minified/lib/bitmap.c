// SPDX-License-Identifier: GPL-2.0-only
/*
 * lib/bitmap.c
 * Helper functions for bitmap.h.
 */

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

/**
 * DOC: bitmap introduction
 *
 * bitmaps provide an array of bits, implemented using an
 * array of unsigned longs.  The number of valid bits in a
 * given bitmap does _not_ need to be an exact multiple of
 * BITS_PER_LONG.
 *
 * The possible unused bits in the last, partially used word
 * of a bitmap are 'don't care'.  The implementation makes
 * no particular effort to keep them zero.  It ensures that
 * their value will not affect the results of any operation.
 * The bitmap operations that return Boolean (bitmap_empty,
 * for example) or scalar (bitmap_weight, for example) results
 * carefully filter out these unused bits from impacting their
 * results.
 *
 * The byte ordering of bitmaps is more natural on little
 * endian architectures.  See the big-endian headers
 * include/asm-ppc64/bitops.h and include/asm-s390/bitops.h
 * for the best explanations of this ordering.
 */

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

bool __bitmap_or_equal(const unsigned long *bitmap1,
		       const unsigned long *bitmap2,
		       const unsigned long *bitmap3,
		       unsigned int bits)
{
	unsigned int k, lim = bits / BITS_PER_LONG;
	unsigned long tmp;

	for (k = 0; k < lim; ++k) {
		if ((bitmap1[k] | bitmap2[k]) != bitmap3[k])
			return false;
	}

	if (!(bits % BITS_PER_LONG))
		return true;

	tmp = (bitmap1[k] | bitmap2[k]) ^ bitmap3[k];
	return (tmp & BITMAP_LAST_WORD_MASK(bits)) == 0;
}

void __bitmap_complement(unsigned long *dst, const unsigned long *src, unsigned int bits)
{
	unsigned int k, lim = BITS_TO_LONGS(bits);
	for (k = 0; k < lim; ++k)
		dst[k] = ~src[k];
}

/**
 * __bitmap_shift_right - logical right shift of the bits in a bitmap
 *   @dst : destination bitmap
 *   @src : source bitmap
 *   @shift : shift by this many bits
 *   @nbits : bitmap size, in bits
 *
 * Shifting right (dividing) means moving bits in the MS -> LS bit
 * direction.  Zeros are fed into the vacated MS positions and the
 * LS bits shifted off the bottom are lost.
 */
void __bitmap_shift_right(unsigned long *dst, const unsigned long *src,
			unsigned shift, unsigned nbits)
{
	unsigned k, lim = BITS_TO_LONGS(nbits);
	unsigned off = shift/BITS_PER_LONG, rem = shift % BITS_PER_LONG;
	unsigned long mask = BITMAP_LAST_WORD_MASK(nbits);
	for (k = 0; off + k < lim; ++k) {
		unsigned long upper, lower;

		/*
		 * If shift is not word aligned, take lower rem bits of
		 * word above and make them the top rem bits of result.
		 */
		if (!rem || off + k + 1 >= lim)
			upper = 0;
		else {
			upper = src[off + k + 1];
			if (off + k + 1 == lim - 1)
				upper &= mask;
			upper <<= (BITS_PER_LONG - rem);
		}
		lower = src[off + k];
		if (off + k == lim - 1)
			lower &= mask;
		lower >>= rem;
		dst[k] = lower | upper;
	}
	if (off)
		memset(&dst[lim - off], 0, off*sizeof(unsigned long));
}


/**
 * __bitmap_shift_left - logical left shift of the bits in a bitmap
 *   @dst : destination bitmap
 *   @src : source bitmap
 *   @shift : shift by this many bits
 *   @nbits : bitmap size, in bits
 *
 * Shifting left (multiplying) means moving bits in the LS -> MS
 * direction.  Zeros are fed into the vacated LS bit positions
 * and those MS bits shifted off the top are lost.
 */

void __bitmap_shift_left(unsigned long *dst, const unsigned long *src,
			unsigned int shift, unsigned int nbits)
{
	int k;
	unsigned int lim = BITS_TO_LONGS(nbits);
	unsigned int off = shift/BITS_PER_LONG, rem = shift % BITS_PER_LONG;
	for (k = lim - off - 1; k >= 0; --k) {
		unsigned long upper, lower;

		/*
		 * If shift is not word aligned, take upper rem bits of
		 * word below and make them the bottom rem bits of result.
		 */
		if (rem && k > 0)
			lower = src[k - 1] >> (BITS_PER_LONG - rem);
		else
			lower = 0;
		upper = src[k] << rem;
		dst[k + off] = lower | upper;
	}
	if (off)
		memset(dst, 0, off*sizeof(unsigned long));
}

/**
 * bitmap_cut() - remove bit region from bitmap and right shift remaining bits
 * @dst: destination bitmap, might overlap with src
 * @src: source bitmap
 * @first: start bit of region to be removed
 * @cut: number of bits to remove
 * @nbits: bitmap size, in bits
 *
 * Set the n-th bit of @dst iff the n-th bit of @src is set and
 * n is less than @first, or the m-th bit of @src is set for any
 * m such that @first <= n < nbits, and m = n + @cut.
 *
 * In pictures, example for a big-endian 32-bit architecture:
 *
 * The @src bitmap is::
 *
 *   31                                   63
 *   |                                    |
 *   10000000 11000001 11110010 00010101  10000000 11000001 01110010 00010101
 *                   |  |              |                                    |
 *                  16  14             0                                   32
 *
 * if @cut is 3, and @first is 14, bits 14-16 in @src are cut and @dst is::
 *
 *   31                                   63
 *   |                                    |
 *   10110000 00011000 00110010 00010101  00010000 00011000 00101110 01000010
 *                      |              |                                    |
 *                      14 (bit 17     0                                   32
 *                          from @src)
 *
 * Note that @dst and @src might overlap partially or entirely.
 *
 * This is implemented in the obvious way, with a shift and carry
 * step for each moved bit. Optimisation is left as an exercise
 * for the compiler.
 */
void bitmap_cut(unsigned long *dst, const unsigned long *src,
		unsigned int first, unsigned int cut, unsigned int nbits)
{
	unsigned int len = BITS_TO_LONGS(nbits);
	unsigned long keep = 0, carry;
	int i;

	if (first % BITS_PER_LONG) {
		keep = src[first / BITS_PER_LONG] &
		       (~0UL >> (BITS_PER_LONG - first % BITS_PER_LONG));
	}

	memmove(dst, src, len * sizeof(*dst));

	while (cut--) {
		for (i = first / BITS_PER_LONG; i < len; i++) {
			if (i < len - 1)
				carry = dst[i + 1] & 1UL;
			else
				carry = 0;

			dst[i] = (dst[i] >> 1) | (carry << (BITS_PER_LONG - 1));
		}
	}

	dst[first / BITS_PER_LONG] &= ~0UL << (first % BITS_PER_LONG);
	dst[first / BITS_PER_LONG] |= keep;
}

int __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int lim = bits/BITS_PER_LONG;
	unsigned long result = 0;

	for (k = 0; k < lim; k++)
		result |= (dst[k] = bitmap1[k] & bitmap2[k]);
	if (bits % BITS_PER_LONG)
		result |= (dst[k] = bitmap1[k] & bitmap2[k] &
			   BITMAP_LAST_WORD_MASK(bits));
	return result != 0;
}

void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] | bitmap2[k];
}

void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] ^ bitmap2[k];
}

int __bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int lim = bits/BITS_PER_LONG;
	unsigned long result = 0;

	for (k = 0; k < lim; k++)
		result |= (dst[k] = bitmap1[k] & ~bitmap2[k]);
	if (bits % BITS_PER_LONG)
		result |= (dst[k] = bitmap1[k] & ~bitmap2[k] &
			   BITMAP_LAST_WORD_MASK(bits));
	return result != 0;
}

void __bitmap_replace(unsigned long *dst,
		      const unsigned long *old, const unsigned long *new,
		      const unsigned long *mask, unsigned int nbits)
{
	unsigned int k;
	unsigned int nr = BITS_TO_LONGS(nbits);

	for (k = 0; k < nr; k++)
		dst[k] = (old[k] & ~mask[k]) | (new[k] & mask[k]);
}

bool __bitmap_intersects(const unsigned long *bitmap1,
			 const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & bitmap2[k])
			return true;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return true;
	return false;
}

bool __bitmap_subset(const unsigned long *bitmap1,
		     const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & ~bitmap2[k])
			return false;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & ~bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return false;
	return true;
}

int __bitmap_weight(const unsigned long *bitmap, unsigned int bits)
{
	unsigned int k, lim = bits/BITS_PER_LONG;
	int w = 0;

	for (k = 0; k < lim; k++)
		w += hweight_long(bitmap[k]);

	if (bits % BITS_PER_LONG)
		w += hweight_long(bitmap[k] & BITMAP_LAST_WORD_MASK(bits));

	return w;
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

/**
 * bitmap_find_next_zero_area_off - find a contiguous aligned zero area
 * @map: The address to base the search on
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr: The number of zeroed bits we're looking for
 * @align_mask: Alignment mask for zero area
 * @align_offset: Alignment offset for zero area.
 *
 * The @align_mask should be one less than a power of 2; the effect is that
 * the bit offset of all zero areas this function finds plus @align_offset
 * is multiple of that power of 2.
 */
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

	/* Align allocation */
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

/*
 * Bitmap printing & parsing functions: first version by Nadia Yvette Chambers,
 * second version by Paul Jackson, third by Joe Korty.
 */

/**
 * bitmap_parse_user - convert an ASCII hex string in a user buffer into a bitmap
 *
 * @ubuf: pointer to user buffer containing string.
 * @ulen: buffer size in bytes.  If string is smaller than this
 *    then it must be terminated with a \0.
 * @maskp: pointer to bitmap array that will contain result.
 * @nmaskbits: size of bitmap, in bits.
 */
int bitmap_parse_user(const char __user *ubuf,
			unsigned int ulen, unsigned long *maskp,
			int nmaskbits)
{
	char *buf;
	int ret;

	buf = memdup_user_nul(ubuf, ulen);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = bitmap_parse(buf, UINT_MAX, maskp, nmaskbits);

	kfree(buf);
	return ret;
}

/**
 * bitmap_print_to_pagebuf - convert bitmap to list or hex format ASCII string
 * @list: indicates whether the bitmap must be list
 * @buf: page aligned buffer into which string is placed
 * @maskp: pointer to bitmap to convert
 * @nmaskbits: size of bitmap, in bits
 *
 * Output format is a comma-separated list of decimal numbers and
 * ranges if list is specified or hex digits grouped into comma-separated
 * sets of 8 digits/set. Returns the number of characters written to buf.
 *
 * It is assumed that @buf is a pointer into a PAGE_SIZE, page-aligned
 * area and that sufficient storage remains at @buf to accommodate the
 * bitmap_print_to_pagebuf() output. Returns the number of characters
 * actually printed to @buf, excluding terminating '\0'.
 */
int bitmap_print_to_pagebuf(bool list, char *buf, const unsigned long *maskp,
			    int nmaskbits)
{
	/* Stubbed for minimal kernel */
	return 0;
}

/**
 * bitmap_print_to_buf  - convert bitmap to list or hex format ASCII string
 * @list: indicates whether the bitmap must be list
 *      true:  print in decimal list format
 *      false: print in hexadecimal bitmask format
 * @buf: buffer into which string is placed
 * @maskp: pointer to bitmap to convert
 * @nmaskbits: size of bitmap, in bits
 * @off: in the string from which we are copying, We copy to @buf
 * @count: the maximum number of bytes to print
 */
/**
 * bitmap_print_bitmask_to_buf  - convert bitmap to hex bitmask format ASCII string
 * @buf: buffer into which string is placed
 * @maskp: pointer to bitmap to convert
 * @nmaskbits: size of bitmap, in bits
 * @off: in the string from which we are copying, We copy to @buf
 * @count: the maximum number of bytes to print
 *
 * The bitmap_print_to_pagebuf() is used indirectly via its cpumap wrapper
 * cpumap_print_to_pagebuf() or directly by drivers to export hexadecimal
 * bitmask and decimal list to userspace by sysfs ABI.
 * Drivers might be using a normal attribute for this kind of ABIs. A
 * normal attribute typically has show entry as below::
 *
 *   static ssize_t example_attribute_show(struct device *dev,
 * 		struct device_attribute *attr, char *buf)
 *   {
 * 	...
 * 	return bitmap_print_to_pagebuf(true, buf, &mask, nr_trig_max);
 *   }
 *
 * show entry of attribute has no offset and count parameters and this
 * means the file is limited to one page only.
 * bitmap_print_to_pagebuf() API works terribly well for this kind of
 * normal attribute with buf parameter and without offset, count::
 *
 *   bitmap_print_to_pagebuf(bool list, char *buf, const unsigned long *maskp,
 * 			   int nmaskbits)
 *   {
 *   }
 *
 * The problem is once we have a large bitmap, we have a chance to get a
 * bitmask or list more than one page. Especially for list, it could be
 * as complex as 0,3,5,7,9,... We have no simple way to know it exact size.
 * It turns out bin_attribute is a way to break this limit. bin_attribute
 * has show entry as below::
 *
 *   static ssize_t
 *   example_bin_attribute_show(struct file *filp, struct kobject *kobj,
 * 		struct bin_attribute *attr, char *buf,
 * 		loff_t offset, size_t count)
 *   {
 * 	...
 *   }
 *
 * With the new offset and count parameters, this makes sysfs ABI be able
 * to support file size more than one page. For example, offset could be
 * >= 4096.
 * bitmap_print_bitmask_to_buf(), bitmap_print_list_to_buf() wit their
 * cpumap wrapper cpumap_print_bitmask_to_buf(), cpumap_print_list_to_buf()
 * make those drivers be able to support large bitmask and list after they
 * move to use bin_attribute. In result, we have to pass the corresponding
 * parameters such as off, count from bin_attribute show entry to this API.
 *
 * The role of cpumap_print_bitmask_to_buf() and cpumap_print_list_to_buf()
 * is similar with cpumap_print_to_pagebuf(),  the difference is that
 * bitmap_print_to_pagebuf() mainly serves sysfs attribute with the assumption
 * the destination buffer is exactly one page and won't be more than one page.
 * cpumap_print_bitmask_to_buf() and cpumap_print_list_to_buf(), on the other
 * hand, mainly serves bin_attribute which doesn't work with exact one page,
 * and it can break the size limit of converted decimal list and hexadecimal
 * bitmask.
 *
 * WARNING!
 *
 * This function is not a replacement for sprintf() or bitmap_print_to_pagebuf().
 * It is intended to workaround sysfs limitations discussed above and should be
 * used carefully in general case for the following reasons:
 *
 *  - Time complexity is O(nbits^2/count), comparing to O(nbits) for snprintf().
 *  - Memory complexity is O(nbits), comparing to O(1) for snprintf().
 *  - @off and @count are NOT offset and number of bits to print.
 *  - If printing part of bitmap as list, the resulting string is not a correct
 *    list representation of bitmap. Particularly, some bits within or out of
 *    related interval may be erroneously set or unset. The format of the string
 *    may be broken, so bitmap_parselist-like parser may fail parsing it.
 *  - If printing the whole bitmap as list by parts, user must ensure the order
 *    of calls of the function such that the offset is incremented linearly.
 *  - If printing the whole bitmap as list by parts, user must keep bitmap
 *    unchanged between the very first and very last call. Otherwise concatenated
 *    result may be incorrect, and format may be broken.
 *
 * Returns the number of characters actually printed to @buf
 */
int bitmap_print_bitmask_to_buf(char *buf, const unsigned long *maskp,
				 int nmaskbits, loff_t off, size_t count)
{
	/* Stubbed for minimal kernel */
	return 0;
}

/**
 * bitmap_print_list_to_buf  - convert bitmap to decimal list format ASCII string
 * @buf: buffer into which string is placed
 * @maskp: pointer to bitmap to convert
 * @nmaskbits: size of bitmap, in bits
 * @off: in the string from which we are copying, We copy to @buf
 * @count: the maximum number of bytes to print
 *
 * Everything is same with the above bitmap_print_bitmask_to_buf() except
 * the print format.
 */
int bitmap_print_list_to_buf(char *buf, const unsigned long *maskp,
			      int nmaskbits, loff_t off, size_t count)
{
	/* Stubbed for minimal kernel */
	return 0;
}

/*
 * Region 9-38:4/10 describes the following bitmap structure:
 * 0	   9  12    18			38	     N
 * .........****......****......****..................
 *	    ^  ^     ^			 ^	     ^
 *      start  off   group_len	       end	 nbits
 */
struct region {
	unsigned int start;
	unsigned int off;
	unsigned int group_len;
	unsigned int end;
	unsigned int nbits;
};

static void bitmap_set_region(const struct region *r, unsigned long *bitmap)
{
	unsigned int start;

	for (start = r->start; start <= r->end; start += r->group_len)
		bitmap_set(bitmap, start, min(r->end - start + 1, r->off));
}

static int bitmap_check_region(const struct region *r)
{
	if (r->start > r->end || r->group_len == 0 || r->off > r->group_len)
		return -EINVAL;

	if (r->end >= r->nbits)
		return -ERANGE;

	return 0;
}

static const char *bitmap_getnum(const char *str, unsigned int *num,
				 unsigned int lastbit)
{
	unsigned long long n;
	unsigned int len;

	if (str[0] == 'N') {
		*num = lastbit;
		return str + 1;
	}

	len = _parse_integer(str, 10, &n);
	if (!len)
		return ERR_PTR(-EINVAL);
	if (len & KSTRTOX_OVERFLOW || n != (unsigned int)n)
		return ERR_PTR(-EOVERFLOW);

	*num = n;
	return str + len;
}

static inline bool end_of_str(char c)
{
	return c == '\0' || c == '\n';
}

static inline bool __end_of_region(char c)
{
	return isspace(c) || c == ',';
}

static inline bool end_of_region(char c)
{
	return __end_of_region(c) || end_of_str(c);
}

/*
 * The format allows commas and whitespaces at the beginning
 * of the region.
 */
static const char *bitmap_find_region(const char *str)
{
	while (__end_of_region(*str))
		str++;

	return end_of_str(*str) ? NULL : str;
}

static const char *bitmap_find_region_reverse(const char *start, const char *end)
{
	while (start <= end && __end_of_region(*end))
		end--;

	return end;
}

static const char *bitmap_parse_region(const char *str, struct region *r)
{
	unsigned int lastbit = r->nbits - 1;

	if (!strncasecmp(str, "all", 3)) {
		r->start = 0;
		r->end = lastbit;
		str += 3;

		goto check_pattern;
	}

	str = bitmap_getnum(str, &r->start, lastbit);
	if (IS_ERR(str))
		return str;

	if (end_of_region(*str))
		goto no_end;

	if (*str != '-')
		return ERR_PTR(-EINVAL);

	str = bitmap_getnum(str + 1, &r->end, lastbit);
	if (IS_ERR(str))
		return str;

check_pattern:
	if (end_of_region(*str))
		goto no_pattern;

	if (*str != ':')
		return ERR_PTR(-EINVAL);

	str = bitmap_getnum(str + 1, &r->off, lastbit);
	if (IS_ERR(str))
		return str;

	if (*str != '/')
		return ERR_PTR(-EINVAL);

	return bitmap_getnum(str + 1, &r->group_len, lastbit);

no_end:
	r->end = r->start;
no_pattern:
	r->off = r->end + 1;
	r->group_len = r->end + 1;

	return end_of_str(*str) ? NULL : str;
}

/**
 * bitmap_parselist - convert list format ASCII string to bitmap
 * @buf: read user string from this buffer; must be terminated
 *    with a \0 or \n.
 * @maskp: write resulting mask here
 * @nmaskbits: number of bits in mask to be written
 *
 * Input format is a comma-separated list of decimal numbers and
 * ranges.  Consecutively set bits are shown as two hyphen-separated
 * decimal numbers, the smallest and largest bit numbers set in
 * the range.
 * Optionally each range can be postfixed to denote that only parts of it
 * should be set. The range will divided to groups of specific size.
 * From each group will be used only defined amount of bits.
 * Syntax: range:used_size/group_size
 * Example: 0-1023:2/256 ==> 0,1,256,257,512,513,768,769
 * The value 'N' can be used as a dynamically substituted token for the
 * maximum allowed value; i.e (nmaskbits - 1).  Keep in mind that it is
 * dynamic, so if system changes cause the bitmap width to change, such
 * as more cores in a CPU list, then any ranges using N will also change.
 *
 * Returns: 0 on success, -errno on invalid input strings. Error values:
 *
 *   - ``-EINVAL``: wrong region format
 *   - ``-EINVAL``: invalid character in string
 *   - ``-ERANGE``: bit number specified too large for mask
 *   - ``-EOVERFLOW``: integer overflow in the input parameters
 */
int bitmap_parselist(const char *buf, unsigned long *maskp, int nmaskbits)
{
	struct region r;
	long ret;

	r.nbits = nmaskbits;
	bitmap_zero(maskp, r.nbits);

	while (buf) {
		buf = bitmap_find_region(buf);
		if (buf == NULL)
			return 0;

		buf = bitmap_parse_region(buf, &r);
		if (IS_ERR(buf))
			return PTR_ERR(buf);

		ret = bitmap_check_region(&r);
		if (ret)
			return ret;

		bitmap_set_region(&r, maskp);
	}

	return 0;
}


/**
 * bitmap_parselist_user() - convert user buffer's list format ASCII
 * string to bitmap
 *
 * @ubuf: pointer to user buffer containing string.
 * @ulen: buffer size in bytes.  If string is smaller than this
 *    then it must be terminated with a \0.
 * @maskp: pointer to bitmap array that will contain result.
 * @nmaskbits: size of bitmap, in bits.
 *
 * Wrapper for bitmap_parselist(), providing it with user buffer.
 */
int bitmap_parselist_user(const char __user *ubuf,
			unsigned int ulen, unsigned long *maskp,
			int nmaskbits)
{
	char *buf;
	int ret;

	buf = memdup_user_nul(ubuf, ulen);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = bitmap_parselist(buf, maskp, nmaskbits);

	kfree(buf);
	return ret;
}

static const char *bitmap_get_x32_reverse(const char *start,
					const char *end, u32 *num)
{
	u32 ret = 0;
	int c, i;

	for (i = 0; i < 32; i += 4) {
		c = hex_to_bin(*end--);
		if (c < 0)
			return ERR_PTR(-EINVAL);

		ret |= c << i;

		if (start > end || __end_of_region(*end))
			goto out;
	}

	if (hex_to_bin(*end--) >= 0)
		return ERR_PTR(-EOVERFLOW);
out:
	*num = ret;
	return end;
}

/**
 * bitmap_parse - convert an ASCII hex string into a bitmap.
 * @start: pointer to buffer containing string.
 * @buflen: buffer size in bytes.  If string is smaller than this
 *    then it must be terminated with a \0 or \n. In that case,
 *    UINT_MAX may be provided instead of string length.
 * @maskp: pointer to bitmap array that will contain result.
 * @nmaskbits: size of bitmap, in bits.
 *
 * Commas group hex digits into chunks.  Each chunk defines exactly 32
 * bits of the resultant bitmask.  No chunk may specify a value larger
 * than 32 bits (%-EOVERFLOW), and if a chunk specifies a smaller value
 * then leading 0-bits are prepended.  %-EINVAL is returned for illegal
 * characters. Grouping such as "1,,5", ",44", "," or "" is allowed.
 * Leading, embedded and trailing whitespace accepted.
 */
int bitmap_parse(const char *start, unsigned int buflen,
		unsigned long *maskp, int nmaskbits)
{
	const char *end = strnchrnul(start, buflen, '\n') - 1;
	int chunks = BITS_TO_U32(nmaskbits);
	u32 *bitmap = (u32 *)maskp;
	int unset_bit;
	int chunk;

	for (chunk = 0; ; chunk++) {
		end = bitmap_find_region_reverse(start, end);
		if (start > end)
			break;

		if (!chunks--)
			return -EOVERFLOW;

		end = bitmap_get_x32_reverse(start, end, &bitmap[chunk]);
		if (IS_ERR(end))
			return PTR_ERR(end);
	}

	unset_bit = (BITS_TO_U32(nmaskbits) - chunks) * 32;
	if (unset_bit < nmaskbits) {
		bitmap_clear(maskp, unset_bit, nmaskbits - unset_bit);
		return 0;
	}

	if (find_next_bit(maskp, unset_bit, nmaskbits) != unset_bit)
		return -EOVERFLOW;

	return 0;
}

/**
 * bitmap_pos_to_ord - find ordinal of set bit at given position in bitmap
 *	@buf: pointer to a bitmap
 *	@pos: a bit position in @buf (0 <= @pos < @nbits)
 *	@nbits: number of valid bit positions in @buf
 *
 * Map the bit at position @pos in @buf (of length @nbits) to the
 * ordinal of which set bit it is.  If it is not set or if @pos
 * is not a valid bit position, map to -1.
 *
 * If for example, just bits 4 through 7 are set in @buf, then @pos
 * values 4 through 7 will get mapped to 0 through 3, respectively,
 * and other @pos values will get mapped to -1.  When @pos value 7
 * gets mapped to (returns) @ord value 3 in this example, that means
 * that bit 7 is the 3rd (starting with 0th) set bit in @buf.
 *
 * The bit positions 0 through @bits are valid positions in @buf.
 */
static int bitmap_pos_to_ord(const unsigned long *buf, unsigned int pos, unsigned int nbits)
{
	if (pos >= nbits || !test_bit(pos, buf))
		return -1;

	return __bitmap_weight(buf, pos);
}

/**
 * bitmap_ord_to_pos - find position of n-th set bit in bitmap
 *	@buf: pointer to bitmap
 *	@ord: ordinal bit position (n-th set bit, n >= 0)
 *	@nbits: number of valid bit positions in @buf
 *
 * Map the ordinal offset of bit @ord in @buf to its position in @buf.
 * Value of @ord should be in range 0 <= @ord < weight(buf). If @ord
 * >= weight(buf), returns @nbits.
 *
 * If for example, just bits 4 through 7 are set in @buf, then @ord
 * values 0 through 3 will get mapped to 4 through 7, respectively,
 * and all other @ord values returns @nbits.  When @ord value 3
 * gets mapped to (returns) @pos value 7 in this example, that means
 * that the 3rd set bit (starting with 0th) is at position 7 in @buf.
 *
 * The bit positions 0 through @nbits-1 are valid positions in @buf.
 */
unsigned int bitmap_ord_to_pos(const unsigned long *buf, unsigned int ord, unsigned int nbits)
{
	unsigned int pos;

	for (pos = find_first_bit(buf, nbits);
	     pos < nbits && ord;
	     pos = find_next_bit(buf, nbits, pos + 1))
		ord--;

	return pos;
}

/**
 * bitmap_remap - Apply map defined by a pair of bitmaps to another bitmap
 *	@dst: remapped result
 *	@src: subset to be remapped
 *	@old: defines domain of map
 *	@new: defines range of map
 *	@nbits: number of bits in each of these bitmaps
 *
 * Let @old and @new define a mapping of bit positions, such that
 * whatever position is held by the n-th set bit in @old is mapped
 * to the n-th set bit in @new.  In the more general case, allowing
 * for the possibility that the weight 'w' of @new is less than the
 * weight of @old, map the position of the n-th set bit in @old to
 * the position of the m-th set bit in @new, where m == n % w.
 *
 * If either of the @old and @new bitmaps are empty, or if @src and
 * @dst point to the same location, then this routine copies @src
 * to @dst.
 *
 * The positions of unset bits in @old are mapped to themselves
 * (the identify map).
 *
 * Apply the above specified mapping to @src, placing the result in
 * @dst, clearing any bits previously set in @dst.
 *
 * For example, lets say that @old has bits 4 through 7 set, and
 * @new has bits 12 through 15 set.  This defines the mapping of bit
 * position 4 to 12, 5 to 13, 6 to 14 and 7 to 15, and of all other
 * bit positions unchanged.  So if say @src comes into this routine
 * with bits 1, 5 and 7 set, then @dst should leave with bits 1,
 * 13 and 15 set.
 */
void bitmap_remap(unsigned long *dst, const unsigned long *src,
		const unsigned long *old, const unsigned long *new,
		unsigned int nbits)
{
	unsigned int oldbit, w;

	if (dst == src)		/* following doesn't handle inplace remaps */
		return;
	bitmap_zero(dst, nbits);

	w = bitmap_weight(new, nbits);
	for_each_set_bit(oldbit, src, nbits) {
		int n = bitmap_pos_to_ord(old, oldbit, nbits);

		if (n < 0 || w == 0)
			set_bit(oldbit, dst);	/* identity map */
		else
			set_bit(bitmap_ord_to_pos(new, n % w, nbits), dst);
	}
}

/**
 * bitmap_bitremap - Apply map defined by a pair of bitmaps to a single bit
 *	@oldbit: bit position to be mapped
 *	@old: defines domain of map
 *	@new: defines range of map
 *	@bits: number of bits in each of these bitmaps
 *
 * Let @old and @new define a mapping of bit positions, such that
 * whatever position is held by the n-th set bit in @old is mapped
 * to the n-th set bit in @new.  In the more general case, allowing
 * for the possibility that the weight 'w' of @new is less than the
 * weight of @old, map the position of the n-th set bit in @old to
 * the position of the m-th set bit in @new, where m == n % w.
 *
 * The positions of unset bits in @old are mapped to themselves
 * (the identify map).
 *
 * Apply the above specified mapping to bit position @oldbit, returning
 * the new bit position.
 *
 * For example, lets say that @old has bits 4 through 7 set, and
 * @new has bits 12 through 15 set.  This defines the mapping of bit
 * position 4 to 12, 5 to 13, 6 to 14 and 7 to 15, and of all other
 * bit positions unchanged.  So if say @oldbit is 5, then this routine
 * returns 13.
 */
int bitmap_bitremap(int oldbit, const unsigned long *old,
				const unsigned long *new, int bits)
{
	int w = bitmap_weight(new, bits);
	int n = bitmap_pos_to_ord(old, oldbit, bits);
	if (n < 0 || w == 0)
		return oldbit;
	else
		return bitmap_ord_to_pos(new, n % w, bits);
}


/*
 * Common code for bitmap_*_region() routines.
 *	bitmap: array of unsigned longs corresponding to the bitmap
 *	pos: the beginning of the region
 *	order: region size (log base 2 of number of bits)
 *	reg_op: operation(s) to perform on that region of bitmap
 *
 * Can set, verify and/or release a region of bits in a bitmap,
 * depending on which combination of REG_OP_* flag bits is set.
 *
 * A region of a bitmap is a sequence of bits in the bitmap, of
 * some size '1 << order' (a power of two), aligned to that same
 * '1 << order' power of two.
 *
 * Returns 1 if REG_OP_ISFREE succeeds (region is all zero bits).
 * Returns 0 in all other cases and reg_ops.
 */

enum {
	REG_OP_ISFREE,		/* true if region is all zero bits */
	REG_OP_ALLOC,		/* set all bits in region */
	REG_OP_RELEASE,		/* clear all bits in region */
};

static int __reg_op(unsigned long *bitmap, unsigned int pos, int order, int reg_op)
{
	int nbits_reg;		/* number of bits in region */
	int index;		/* index first long of region in bitmap */
	int offset;		/* bit offset region in bitmap[index] */
	int nlongs_reg;		/* num longs spanned by region in bitmap */
	int nbitsinlong;	/* num bits of region in each spanned long */
	unsigned long mask;	/* bitmask for one long of region */
	int i;			/* scans bitmap by longs */
	int ret = 0;		/* return value */

	/*
	 * Either nlongs_reg == 1 (for small orders that fit in one long)
	 * or (offset == 0 && mask == ~0UL) (for larger multiword orders.)
	 */
	nbits_reg = 1 << order;
	index = pos / BITS_PER_LONG;
	offset = pos - (index * BITS_PER_LONG);
	nlongs_reg = BITS_TO_LONGS(nbits_reg);
	nbitsinlong = min(nbits_reg,  BITS_PER_LONG);

	/*
	 * Can't do "mask = (1UL << nbitsinlong) - 1", as that
	 * overflows if nbitsinlong == BITS_PER_LONG.
	 */
	mask = (1UL << (nbitsinlong - 1));
	mask += mask - 1;
	mask <<= offset;

	switch (reg_op) {
	case REG_OP_ISFREE:
		for (i = 0; i < nlongs_reg; i++) {
			if (bitmap[index + i] & mask)
				goto done;
		}
		ret = 1;	/* all bits in region free (zero) */
		break;

	case REG_OP_ALLOC:
		for (i = 0; i < nlongs_reg; i++)
			bitmap[index + i] |= mask;
		break;

	case REG_OP_RELEASE:
		for (i = 0; i < nlongs_reg; i++)
			bitmap[index + i] &= ~mask;
		break;
	}
done:
	return ret;
}

/**
 * bitmap_find_free_region - find a contiguous aligned mem region
 *	@bitmap: array of unsigned longs corresponding to the bitmap
 *	@bits: number of bits in the bitmap
 *	@order: region size (log base 2 of number of bits) to find
 *
 * Find a region of free (zero) bits in a @bitmap of @bits bits and
 * allocate them (set them to one).  Only consider regions of length
 * a power (@order) of two, aligned to that power of two, which
 * makes the search algorithm much faster.
 *
 * Return the bit offset in bitmap of the allocated region,
 * or -errno on failure.
 */
int bitmap_find_free_region(unsigned long *bitmap, unsigned int bits, int order)
{
	unsigned int pos, end;		/* scans bitmap by regions of size order */

	for (pos = 0 ; (end = pos + (1U << order)) <= bits; pos = end) {
		if (!__reg_op(bitmap, pos, order, REG_OP_ISFREE))
			continue;
		__reg_op(bitmap, pos, order, REG_OP_ALLOC);
		return pos;
	}
	return -ENOMEM;
}

/**
 * bitmap_release_region - release allocated bitmap region
 *	@bitmap: array of unsigned longs corresponding to the bitmap
 *	@pos: beginning of bit region to release
 *	@order: region size (log base 2 of number of bits) to release
 *
 * This is the complement to __bitmap_find_free_region() and releases
 * the found region (by clearing it in the bitmap).
 *
 * No return value.
 */
void bitmap_release_region(unsigned long *bitmap, unsigned int pos, int order)
{
	__reg_op(bitmap, pos, order, REG_OP_RELEASE);
}

/**
 * bitmap_allocate_region - allocate bitmap region
 *	@bitmap: array of unsigned longs corresponding to the bitmap
 *	@pos: beginning of bit region to allocate
 *	@order: region size (log base 2 of number of bits) to allocate
 *
 * Allocate (set bits in) a specified region of a bitmap.
 *
 * Return 0 on success, or %-EBUSY if specified region wasn't
 * free (not all bits were zero).
 */
int bitmap_allocate_region(unsigned long *bitmap, unsigned int pos, int order)
{
	if (!__reg_op(bitmap, pos, order, REG_OP_ISFREE))
		return -EBUSY;
	return __reg_op(bitmap, pos, order, REG_OP_ALLOC);
}

/**
 * bitmap_copy_le - copy a bitmap, putting the bits into little-endian order.
 * @dst:   destination buffer
 * @src:   bitmap to copy
 * @nbits: number of bits in the bitmap
 *
 * Require nbits % BITS_PER_LONG == 0.
 */
#ifdef __BIG_ENDIAN
void bitmap_copy_le(unsigned long *dst, const unsigned long *src, unsigned int nbits)
{
	unsigned int i;

	for (i = 0; i < nbits/BITS_PER_LONG; i++) {
		if (BITS_PER_LONG == 64)
			dst[i] = cpu_to_le64(src[i]);
		else
			dst[i] = cpu_to_le32(src[i]);
	}
}
#endif

unsigned long *bitmap_alloc(unsigned int nbits, gfp_t flags)
{
	return kmalloc_array(BITS_TO_LONGS(nbits), sizeof(unsigned long),
			     flags);
}

unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags)
{
	return bitmap_alloc(nbits, flags | __GFP_ZERO);
}

unsigned long *bitmap_alloc_node(unsigned int nbits, gfp_t flags, int node)
{
	return kmalloc_array_node(BITS_TO_LONGS(nbits), sizeof(unsigned long),
				  flags, node);
}

unsigned long *bitmap_zalloc_node(unsigned int nbits, gfp_t flags, int node)
{
	return bitmap_alloc_node(nbits, flags | __GFP_ZERO, node);
}

void bitmap_free(const unsigned long *bitmap)
{
	kfree(bitmap);
}

static void devm_bitmap_free(void *data)
{
	unsigned long *bitmap = data;

	bitmap_free(bitmap);
}

unsigned long *devm_bitmap_alloc(struct device *dev,
				 unsigned int nbits, gfp_t flags)
{
	unsigned long *bitmap;
	int ret;

	bitmap = bitmap_alloc(nbits, flags);
	if (!bitmap)
		return NULL;

	ret = devm_add_action_or_reset(dev, devm_bitmap_free, bitmap);
	if (ret)
		return NULL;

	return bitmap;
}

unsigned long *devm_bitmap_zalloc(struct device *dev,
				  unsigned int nbits, gfp_t flags)
{
	return devm_bitmap_alloc(dev, nbits, flags | __GFP_ZERO);
}

#if BITS_PER_LONG == 64
/**
 * bitmap_from_arr32 - copy the contents of u32 array of bits to bitmap
 *	@bitmap: array of unsigned longs, the destination bitmap
 *	@buf: array of u32 (in host byte order), the source bitmap
 *	@nbits: number of bits in @bitmap
 */
void bitmap_from_arr32(unsigned long *bitmap, const u32 *buf, unsigned int nbits)
{
	unsigned int i, halfwords;

	halfwords = DIV_ROUND_UP(nbits, 32);
	for (i = 0; i < halfwords; i++) {
		bitmap[i/2] = (unsigned long) buf[i];
		if (++i < halfwords)
			bitmap[i/2] |= ((unsigned long) buf[i]) << 32;
	}

	/* Clear tail bits in last word beyond nbits. */
	if (nbits % BITS_PER_LONG)
		bitmap[(halfwords - 1) / 2] &= BITMAP_LAST_WORD_MASK(nbits);
}

/**
 * bitmap_to_arr32 - copy the contents of bitmap to a u32 array of bits
 *	@buf: array of u32 (in host byte order), the dest bitmap
 *	@bitmap: array of unsigned longs, the source bitmap
 *	@nbits: number of bits in @bitmap
 */
void bitmap_to_arr32(u32 *buf, const unsigned long *bitmap, unsigned int nbits)
{
	unsigned int i, halfwords;

	halfwords = DIV_ROUND_UP(nbits, 32);
	for (i = 0; i < halfwords; i++) {
		buf[i] = (u32) (bitmap[i/2] & UINT_MAX);
		if (++i < halfwords)
			buf[i] = (u32) (bitmap[i/2] >> 32);
	}

	/* Clear tail bits in last element of array beyond nbits. */
	if (nbits % BITS_PER_LONG)
		buf[halfwords - 1] &= (u32) (UINT_MAX >> ((-nbits) & 31));
}
#endif

#if (BITS_PER_LONG == 32) && defined(__BIG_ENDIAN)
/**
 * bitmap_from_arr64 - copy the contents of u64 array of bits to bitmap
 *	@bitmap: array of unsigned longs, the destination bitmap
 *	@buf: array of u64 (in host byte order), the source bitmap
 *	@nbits: number of bits in @bitmap
 */
void bitmap_from_arr64(unsigned long *bitmap, const u64 *buf, unsigned int nbits)
{
	int n;

	for (n = nbits; n > 0; n -= 64) {
		u64 val = *buf++;

		*bitmap++ = val;
		if (n > 32)
			*bitmap++ = val >> 32;
	}

	/*
	 * Clear tail bits in the last word beyond nbits.
	 *
	 * Negative index is OK because here we point to the word next
	 * to the last word of the bitmap, except for nbits == 0, which
	 * is tested implicitly.
	 */
	if (nbits % BITS_PER_LONG)
		bitmap[-1] &= BITMAP_LAST_WORD_MASK(nbits);
}

/**
 * bitmap_to_arr64 - copy the contents of bitmap to a u64 array of bits
 *	@buf: array of u64 (in host byte order), the dest bitmap
 *	@bitmap: array of unsigned longs, the source bitmap
 *	@nbits: number of bits in @bitmap
 */
void bitmap_to_arr64(u64 *buf, const unsigned long *bitmap, unsigned int nbits)
{
	const unsigned long *end = bitmap + BITS_TO_LONGS(nbits);

	while (bitmap < end) {
		*buf = *bitmap++;
		if (bitmap < end)
			*buf |= (u64)(*bitmap++) << 32;
		buf++;
	}

	/* Clear tail bits in the last element of array beyond nbits. */
	if (nbits % 64)
		buf[-1] &= GENMASK_ULL(nbits % 64, 0);
}
#endif
