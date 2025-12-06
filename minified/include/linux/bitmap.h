#ifndef __LINUX_BITMAP_H
#define __LINUX_BITMAP_H

#ifndef __ASSEMBLY__

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/limits.h>

/* Inlined from find.h */
extern unsigned long _find_next_bit(const unsigned long *addr1,
		const unsigned long *addr2, unsigned long nbits,
		unsigned long start, unsigned long invert, unsigned long le);
extern unsigned long _find_first_bit(const unsigned long *addr, unsigned long size);
extern unsigned long _find_first_and_bit(const unsigned long *addr1,
					 const unsigned long *addr2, unsigned long size);
extern unsigned long _find_first_zero_bit(const unsigned long *addr, unsigned long size);
extern unsigned long _find_last_bit(const unsigned long *addr, unsigned long size);

#ifndef find_next_bit
static inline
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = *addr & GENMASK(size - 1, offset);
		return val ? __ffs(val) : size;
	}

	return _find_next_bit(addr, NULL, size, offset, 0UL, 0);
}
#endif

#ifndef find_next_and_bit
static inline
unsigned long find_next_and_bit(const unsigned long *addr1,
		const unsigned long *addr2, unsigned long size,
		unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = *addr1 & *addr2 & GENMASK(size - 1, offset);
		return val ? __ffs(val) : size;
	}

	return _find_next_bit(addr1, addr2, size, offset, 0UL, 0);
}
#endif

#ifndef find_next_zero_bit
static inline
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
				 unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = *addr | ~GENMASK(size - 1, offset);
		return val == ~0UL ? size : ffz(val);
	}

	return _find_next_bit(addr, NULL, size, offset, ~0UL, 0);
}
#endif

#ifndef find_first_bit
static inline
unsigned long find_first_bit(const unsigned long *addr, unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr & GENMASK(size - 1, 0);

		return val ? __ffs(val) : size;
	}

	return _find_first_bit(addr, size);
}
#endif

#ifndef find_first_and_bit
static inline
unsigned long find_first_and_bit(const unsigned long *addr1,
				 const unsigned long *addr2,
				 unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr1 & *addr2 & GENMASK(size - 1, 0);

		return val ? __ffs(val) : size;
	}

	return _find_first_and_bit(addr1, addr2, size);
}
#endif

#ifndef find_first_zero_bit
static inline
unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr | ~GENMASK(size - 1, 0);

		return val == ~0UL ? size : ffz(val);
	}

	return _find_first_zero_bit(addr, size);
}
#endif

#ifndef find_last_bit
static inline
unsigned long find_last_bit(const unsigned long *addr, unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr & GENMASK(size - 1, 0);

		return val ? __fls(val) : size;
	}

	return _find_last_bit(addr, size);
}
#endif

extern unsigned long find_next_clump8(unsigned long *clump,
				      const unsigned long *addr,
				      unsigned long size, unsigned long offset);

#define find_first_clump8(clump, bits, size) \
	find_next_clump8((clump), (bits), (size), 0)

#if defined(__LITTLE_ENDIAN)

static inline unsigned long find_next_zero_bit_le(const void *addr,
		unsigned long size, unsigned long offset)
{
	return find_next_zero_bit(addr, size, offset);
}

static inline unsigned long find_next_bit_le(const void *addr,
		unsigned long size, unsigned long offset)
{
	return find_next_bit(addr, size, offset);
}

static inline unsigned long find_first_zero_bit_le(const void *addr,
		unsigned long size)
{
	return find_first_zero_bit(addr, size);
}

#elif defined(__BIG_ENDIAN)

#ifndef find_next_zero_bit_le
static inline
unsigned long find_next_zero_bit_le(const void *addr, unsigned
		long size, unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val = *(const unsigned long *)addr;

		if (unlikely(offset >= size))
			return size;

		val = swab(val) | ~GENMASK(size - 1, offset);
		return val == ~0UL ? size : ffz(val);
	}

	return _find_next_bit(addr, NULL, size, offset, ~0UL, 1);
}
#endif

#ifndef find_next_bit_le
static inline
unsigned long find_next_bit_le(const void *addr, unsigned
		long size, unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val = *(const unsigned long *)addr;

		if (unlikely(offset >= size))
			return size;

		val = swab(val) & GENMASK(size - 1, offset);
		return val ? __ffs(val) : size;
	}

	return _find_next_bit(addr, NULL, size, offset, 0UL, 1);
}
#endif

#ifndef find_first_zero_bit_le
#define find_first_zero_bit_le(addr, size) \
	find_next_zero_bit_le((addr), (size), 0)
#endif

#else
#error "Please fix <asm/byteorder.h>"
#endif

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), 0);		\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

#define for_each_set_bit_from(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

#define for_each_clear_bit_from(bit, addr, size) \
	for ((bit) = find_next_zero_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

#define for_each_set_bitrange(b, e, addr, size)			\
	for ((b) = find_next_bit((addr), (size), 0),		\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1);	\
	     (b) < (size);					\
	     (b) = find_next_bit((addr), (size), (e) + 1),	\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1))

#define for_each_set_bitrange_from(b, e, addr, size)		\
	for ((b) = find_next_bit((addr), (size), (b)),		\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1);	\
	     (b) < (size);					\
	     (b) = find_next_bit((addr), (size), (e) + 1),	\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1))

#define for_each_clear_bitrange(b, e, addr, size)		\
	for ((b) = find_next_zero_bit((addr), (size), 0),	\
	     (e) = find_next_bit((addr), (size), (b) + 1);	\
	     (b) < (size);					\
	     (b) = find_next_zero_bit((addr), (size), (e) + 1),	\
	     (e) = find_next_bit((addr), (size), (b) + 1))

#define for_each_clear_bitrange_from(b, e, addr, size)		\
	for ((b) = find_next_zero_bit((addr), (size), (b)),	\
	     (e) = find_next_bit((addr), (size), (b) + 1);	\
	     (b) < (size);					\
	     (b) = find_next_zero_bit((addr), (size), (e) + 1),	\
	     (e) = find_next_bit((addr), (size), (b) + 1))

#define for_each_set_clump8(start, clump, bits, size) \
	for ((start) = find_first_clump8(&(clump), (bits), (size)); \
	     (start) < (size); \
	     (start) = find_next_clump8(&(clump), (bits), (size), (start) + 8))
/* End of inlined find.h content */
#include <linux/string.h>
#include <linux/types.h>

struct device;





unsigned long *bitmap_alloc(unsigned int nbits, gfp_t flags);
unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags);
unsigned long *bitmap_alloc_node(unsigned int nbits, gfp_t flags, int node);
unsigned long *bitmap_zalloc_node(unsigned int nbits, gfp_t flags, int node);
void bitmap_free(const unsigned long *bitmap);

unsigned long *devm_bitmap_alloc(struct device *dev,
				 unsigned int nbits, gfp_t flags);
unsigned long *devm_bitmap_zalloc(struct device *dev,
				  unsigned int nbits, gfp_t flags);


bool __bitmap_equal(const unsigned long *bitmap1,
		    const unsigned long *bitmap2, unsigned int nbits);
/* __bitmap_or_equal, bitmap_cut removed - unused */
int __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
		 const unsigned long *bitmap2, unsigned int nbits);
void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
		 const unsigned long *bitmap2, unsigned int nbits);
void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
		  const unsigned long *bitmap2, unsigned int nbits);
int __bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
		    const unsigned long *bitmap2, unsigned int nbits);
void __bitmap_replace(unsigned long *dst,
		      const unsigned long *old, const unsigned long *new,
		      const unsigned long *mask, unsigned int nbits);
bool __bitmap_intersects(const unsigned long *bitmap1,
			 const unsigned long *bitmap2, unsigned int nbits);
bool __bitmap_subset(const unsigned long *bitmap1,
		     const unsigned long *bitmap2, unsigned int nbits);
int __bitmap_weight(const unsigned long *bitmap, unsigned int nbits);
void __bitmap_set(unsigned long *map, unsigned int start, int len);
void __bitmap_clear(unsigned long *map, unsigned int start, int len);

unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset);

static inline unsigned long
bitmap_find_next_zero_area(unsigned long *map,
			   unsigned long size,
			   unsigned long start,
			   unsigned int nr,
			   unsigned long align_mask)
{
	return bitmap_find_next_zero_area_off(map, size, start, nr,
					      align_mask, 0);
}

int bitmap_parse(const char *buf, unsigned int buflen,
			unsigned long *dst, int nbits);
/* bitmap_parse_user, bitmap_parselist, bitmap_parselist_user, bitmap_remap,
 * bitmap_bitremap, bitmap_onto, bitmap_fold, bitmap_find_free_region,
 * bitmap_release_region, bitmap_allocate_region removed - unused */

#ifdef __BIG_ENDIAN
void bitmap_copy_le(unsigned long *dst, const unsigned long *src, unsigned int nbits);
#else
#define bitmap_copy_le bitmap_copy
#endif
unsigned int bitmap_ord_to_pos(const unsigned long *bitmap, unsigned int ord, unsigned int nbits);
int bitmap_print_to_pagebuf(bool list, char *buf,
				   const unsigned long *maskp, int nmaskbits);

extern int bitmap_print_bitmask_to_buf(char *buf, const unsigned long *maskp,
				      int nmaskbits, loff_t off, size_t count);

extern int bitmap_print_list_to_buf(char *buf, const unsigned long *maskp,
				      int nmaskbits, loff_t off, size_t count);

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (BITS_PER_LONG - 1)))

static inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{
	unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
	memset(dst, 0, len);
}

static inline void bitmap_fill(unsigned long *dst, unsigned int nbits)
{
	unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
	memset(dst, 0xff, len);
}

static inline void bitmap_copy(unsigned long *dst, const unsigned long *src,
			unsigned int nbits)
{
	unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
	memcpy(dst, src, len);
}

static inline void bitmap_copy_clear_tail(unsigned long *dst,
		const unsigned long *src, unsigned int nbits)
{
	bitmap_copy(dst, src, nbits);
	if (nbits % BITS_PER_LONG)
		dst[nbits / BITS_PER_LONG] &= BITMAP_LAST_WORD_MASK(nbits);
}

/* 32-bit only kernel - simplified arr32 macros */
#define bitmap_from_arr32(bitmap, buf, nbits)			\
	bitmap_copy_clear_tail((unsigned long *) (bitmap),	\
			(const unsigned long *) (buf), (nbits))
#define bitmap_to_arr32(buf, bitmap, nbits)			\
	bitmap_copy_clear_tail((unsigned long *) (buf),		\
			(const unsigned long *) (bitmap), (nbits))

#if (BITS_PER_LONG == 32) && defined(__BIG_ENDIAN)
void bitmap_from_arr64(unsigned long *bitmap, const u64 *buf, unsigned int nbits);
void bitmap_to_arr64(u64 *buf, const unsigned long *bitmap, unsigned int nbits);
#else
#define bitmap_from_arr64(bitmap, buf, nbits)			\
	bitmap_copy_clear_tail((unsigned long *)(bitmap), (const unsigned long *)(buf), (nbits))
#define bitmap_to_arr64(buf, bitmap, nbits)			\
	bitmap_copy_clear_tail((unsigned long *)(buf), (const unsigned long *)(bitmap), (nbits))
#endif

static inline int bitmap_and(unsigned long *dst, const unsigned long *src1,
			const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return (*dst = *src1 & *src2 & BITMAP_LAST_WORD_MASK(nbits)) != 0;
	return __bitmap_and(dst, src1, src2, nbits);
}

static inline void bitmap_or(unsigned long *dst, const unsigned long *src1,
			const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		*dst = *src1 | *src2;
	else
		__bitmap_or(dst, src1, src2, nbits);
}

static inline void bitmap_xor(unsigned long *dst, const unsigned long *src1,
			const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		*dst = *src1 ^ *src2;
	else
		__bitmap_xor(dst, src1, src2, nbits);
}

static inline int bitmap_andnot(unsigned long *dst, const unsigned long *src1,
			const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return (*dst = *src1 & ~(*src2) & BITMAP_LAST_WORD_MASK(nbits)) != 0;
	return __bitmap_andnot(dst, src1, src2, nbits);
}


#ifdef __LITTLE_ENDIAN
#define BITMAP_MEM_ALIGNMENT 8
#else
#define BITMAP_MEM_ALIGNMENT (8 * sizeof(unsigned long))
#endif
#define BITMAP_MEM_MASK (BITMAP_MEM_ALIGNMENT - 1)

static inline bool bitmap_equal(const unsigned long *src1,
				const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return !((*src1 ^ *src2) & BITMAP_LAST_WORD_MASK(nbits));
	if (__builtin_constant_p(nbits & BITMAP_MEM_MASK) &&
	    IS_ALIGNED(nbits, BITMAP_MEM_ALIGNMENT))
		return !memcmp(src1, src2, nbits / 8);
	return __bitmap_equal(src1, src2, nbits);
}

static inline bool bitmap_intersects(const unsigned long *src1,
				     const unsigned long *src2,
				     unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return ((*src1 & *src2) & BITMAP_LAST_WORD_MASK(nbits)) != 0;
	else
		return __bitmap_intersects(src1, src2, nbits);
}

static inline bool bitmap_subset(const unsigned long *src1,
				 const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return ! ((*src1 & ~(*src2)) & BITMAP_LAST_WORD_MASK(nbits));
	else
		return __bitmap_subset(src1, src2, nbits);
}

static inline bool bitmap_empty(const unsigned long *src, unsigned nbits)
{
	if (small_const_nbits(nbits))
		return ! (*src & BITMAP_LAST_WORD_MASK(nbits));

	return find_first_bit(src, nbits) == nbits;
}

static inline bool bitmap_full(const unsigned long *src, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return ! (~(*src) & BITMAP_LAST_WORD_MASK(nbits));

	return find_first_zero_bit(src, nbits) == nbits;
}

static __always_inline int bitmap_weight(const unsigned long *src, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return hweight_long(*src & BITMAP_LAST_WORD_MASK(nbits));
	return __bitmap_weight(src, nbits);
}

static __always_inline void bitmap_set(unsigned long *map, unsigned int start,
		unsigned int nbits)
{
	if (__builtin_constant_p(nbits) && nbits == 1)
		__set_bit(start, map);
	else if (__builtin_constant_p(start & BITMAP_MEM_MASK) &&
		 IS_ALIGNED(start, BITMAP_MEM_ALIGNMENT) &&
		 __builtin_constant_p(nbits & BITMAP_MEM_MASK) &&
		 IS_ALIGNED(nbits, BITMAP_MEM_ALIGNMENT))
		memset((char *)map + start / 8, 0xff, nbits / 8);
	else
		__bitmap_set(map, start, nbits);
}

static __always_inline void bitmap_clear(unsigned long *map, unsigned int start,
		unsigned int nbits)
{
	if (__builtin_constant_p(nbits) && nbits == 1)
		__clear_bit(start, map);
	else if (__builtin_constant_p(start & BITMAP_MEM_MASK) &&
		 IS_ALIGNED(start, BITMAP_MEM_ALIGNMENT) &&
		 __builtin_constant_p(nbits & BITMAP_MEM_MASK) &&
		 IS_ALIGNED(nbits, BITMAP_MEM_ALIGNMENT))
		memset((char *)map + start / 8, 0, nbits / 8);
	else
		__bitmap_clear(map, start, nbits);
}
/* 32-bit only kernel - BITMAP_FROM_U64 expands to two words */
#define BITMAP_FROM_U64(n) ((unsigned long) ((u64)(n) & ULONG_MAX)), \
				((unsigned long) ((u64)(n) >> 32))

static inline unsigned long bitmap_get_value8(const unsigned long *map,
					      unsigned long start)
{
	const size_t index = BIT_WORD(start);
	const unsigned long offset = start % BITS_PER_LONG;

	return (map[index] >> offset) & 0xFF;
}

#endif

#endif  
