#ifndef _LINUX_BITOPS_H
#define _LINUX_BITOPS_H

#include <asm/types.h>
#include <linux/bits.h>
#include <linux/typecheck.h>

#include <linux/const.h>

#ifdef __LITTLE_ENDIAN
#  define aligned_byte_mask(n) ((1UL << 8*(n))-1)
#else
#  define aligned_byte_mask(n) (~0xffUL << (BITS_PER_LONG - 8 - 8*(n)))
#endif

#define BITS_PER_TYPE(type)	(sizeof(type) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)	__KERNEL_DIV_ROUND_UP(nr, BITS_PER_TYPE(long))
/* __sw_hweight8/16/32/64 removed - unused software fallback declarations */

#include <asm/bitops.h>

/* get_bitmask_order, hweight_long removed - unused */

/* rol64, ror64, ror32, rol16, ror16, rol8, ror8 removed - never called */

static inline __u32 rol32(__u32 word, unsigned int shift)
{
	return (word << (shift & 31)) | (word >> ((-shift) & 31));
}

/* sign_extend32, sign_extend64 - unused */

static inline unsigned fls_long(unsigned long l)
{
	if (sizeof(l) == 4)
		return fls(l);
	return fls64(l);
}

/* get_count_order - unused */

static inline int get_count_order_long(unsigned long l)
{
	if (l == 0UL)
		return -1;
	return (int)fls_long(--l);
}

/* __ffs64 - unused */

static __always_inline void assign_bit(long nr, volatile unsigned long *addr,
				       bool value)
{
	if (value)
		set_bit(nr, addr);
	else
		clear_bit(nr, addr);
}

static __always_inline void __assign_bit(long nr, volatile unsigned long *addr,
					 bool value)
{
	if (value)
		__set_bit(nr, addr);
	else
		__clear_bit(nr, addr);
}


#ifdef __KERNEL__

#ifndef set_mask_bits
#define set_mask_bits(ptr, mask, bits)	\
({								\
	const typeof(*(ptr)) mask__ = (mask), bits__ = (bits);	\
	typeof(*(ptr)) old__, new__;				\
								\
	do {							\
		old__ = READ_ONCE(*(ptr));			\
		new__ = (old__ & ~mask__) | bits__;		\
	} while (cmpxchg(ptr, old__, new__) != old__);		\
								\
	old__;							\
})
#endif


#endif  
#endif
