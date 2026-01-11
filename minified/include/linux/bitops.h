#ifndef _LINUX_BITOPS_H
#define _LINUX_BITOPS_H
#include <asm/types.h>
#include <linux/bits.h>
#include <linux/typecheck.h>
#include <linux/const.h>
#define aligned_byte_mask(n) ((1UL << 8*(n))-1)
#define BITS_PER_TYPE(type)	(sizeof(type) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)	__KERNEL_DIV_ROUND_UP(nr, BITS_PER_TYPE(long))
#include <asm/bitops.h>
static inline __u32 rol32(__u32 word, unsigned int shift) { return (word << (shift & 31)) | (word >> ((-shift) & 31)); }
static inline unsigned fls_long(unsigned long l) { if (sizeof(l) == 4) return fls(l); return fls64(l); }
static inline int get_count_order_long(unsigned long l) { if (l == 0UL) return -1; return (int)fls_long(--l); }
/* assign_bit, set_mask_bits removed - unused */
#endif
