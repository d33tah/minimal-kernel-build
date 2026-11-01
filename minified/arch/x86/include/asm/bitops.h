/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BITOPS_H
#define _ASM_X86_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 *
 * Note: inlines with more than a single statement should be marked
 * __always_inline to avoid problems with older gcc's inlining heuristics.
 */

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm/asm.h>
#include <asm/rmwcc.h>
#include <asm/barrier.h>

/* BITS_PER_LONG == 32 (i386) */
#define _BITOPS_LONG_SHIFT 5

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

#define RLONG_ADDR(x) "m"(*(volatile long *)(x))
#define WBYTE_ADDR(x) "+m"(*(volatile char *)(x))

#define ADDR RLONG_ADDR(addr)

/*
 * We do the locked ops that don't return the old value as
 * a mask operation on a byte.
 */
#define CONST_MASK_ADDR(nr, addr) WBYTE_ADDR((void *)(addr) + ((nr) >> 3))
#define CONST_MASK(nr) (1 << ((nr) & 7))

static __always_inline void arch_set_bit(long nr, volatile unsigned long *addr)
{
	if (__builtin_constant_p(nr)) {
		asm volatile("orb %b1,%0"
			     : CONST_MASK_ADDR(nr, addr)
			     : "iq"(CONST_MASK(nr))
			     : "memory");
	} else {
		asm volatile(__ASM_SIZE(bts) " %1,%0"
			     :
			     : RLONG_ADDR(addr), "Ir"(nr)
			     : "memory");
	}
}

static __always_inline void arch___set_bit(long nr,
					   volatile unsigned long *addr)
{
	asm volatile(__ASM_SIZE(bts) " %1,%0" : : ADDR, "Ir"(nr) : "memory");
}

static __always_inline void arch_clear_bit(long nr,
					   volatile unsigned long *addr)
{
	if (__builtin_constant_p(nr)) {
		asm volatile("andb %b1,%0"
			     : CONST_MASK_ADDR(nr, addr)
			     : "iq"(~CONST_MASK(nr)));
	} else {
		asm volatile(__ASM_SIZE(btr) " %1,%0"
			     :
			     : RLONG_ADDR(addr), "Ir"(nr)
			     : "memory");
	}
}

static __always_inline void arch___clear_bit(long nr,
					     volatile unsigned long *addr)
{
	asm volatile(__ASM_SIZE(btr) " %1,%0" : : ADDR, "Ir"(nr) : "memory");
}

static __always_inline bool
arch_clear_bit_unlock_is_negative_byte(long nr, volatile unsigned long *addr)
{
	bool negative;
	asm volatile("andb %2,%1" CC_SET(s)
		     : CC_OUT(s)(negative), WBYTE_ADDR(addr)
		     : "ir"((char)~(1 << nr))
		     : "memory");
	return negative;
}
#define arch_clear_bit_unlock_is_negative_byte \
	arch_clear_bit_unlock_is_negative_byte

static __always_inline bool arch_test_and_set_bit(long nr,
						  volatile unsigned long *addr)
{
	return GEN_BINARY_RMWcc(__ASM_SIZE(bts), *addr, c, "Ir", nr);
}

static __always_inline bool
arch_test_and_set_bit_lock(long nr, volatile unsigned long *addr)
{
	return arch_test_and_set_bit(nr, addr);
}

static __always_inline bool
constant_test_bit(long nr, const volatile unsigned long *addr)
{
	return ((1UL << (nr & (BITS_PER_LONG - 1))) &
		(addr[nr >> _BITOPS_LONG_SHIFT])) != 0;
}

static __always_inline bool
variable_test_bit(long nr, volatile const unsigned long *addr)
{
	bool oldbit;

	asm volatile(__ASM_SIZE(bt) " %2,%1" CC_SET(c)
		     : CC_OUT(c)(oldbit)
		     : "m"(*(unsigned long *)addr), "Ir"(nr)
		     : "memory");

	return oldbit;
}

#define arch_test_bit(nr, addr)                                         \
	(__builtin_constant_p((nr)) ? constant_test_bit((nr), (addr)) : \
				      variable_test_bit((nr), (addr)))

/**
 * __ffs - find first set bit in word
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static __always_inline unsigned long __ffs(unsigned long word)
{
	asm("rep; bsf %1,%0" : "=r"(word) : "rm"(word));
	return word;
}

/**
 * ffz - find first zero bit in word
 * @word: The word to search
 *
 * Undefined if no zero exists, so code should check against ~0UL first.
 */
static __always_inline unsigned long ffz(unsigned long word)
{
	asm("rep; bsf %1,%0" : "=r"(word) : "r"(~word));
	return word;
}

#undef ADDR

/**
 * ffs - find first set bit in word
 * @x: the word to search
 *
 * This is defined the same way as the libc and compiler builtin ffs
 * routines, therefore differs in spirit from the other bitops.
 *
 * ffs(value) returns 0 if value is 0 or the position of the first
 * set bit if value is nonzero. The first (least significant) bit
 * is at position 1.
 */
static __always_inline int ffs(int x)
{
	int r;

	asm("bsfl %1,%0\n\t"
	    "jnz 1f\n\t"
	    "movl $-1,%0\n"
	    "1:"
	    : "=r"(r)
	    : "rm"(x));
	return r + 1;
}

/**
 * fls - find last set bit in word
 * @x: the word to search
 *
 * This is defined in a similar way as the libc and compiler builtin
 * ffs, but returns the position of the most significant set bit.
 *
 * fls(value) returns 0 if value is 0 or the position of the last
 * set bit if value is nonzero. The last (most significant) bit is
 * at position 32.
 */
static __always_inline int fls(unsigned int x)
{
	int r;

	asm("bsrl %1,%0\n\t"
	    "jnz 1f\n\t"
	    "movl $-1,%0\n"
	    "1:"
	    : "=r"(r)
	    : "rm"(x));
	return r + 1;
}

static __always_inline int fls64(__u64 x)
{
	__u32 h = x >> 32;
	if (h)
		return fls(h) + 32;
	return fls(x);
}

static __always_inline void set_bit(long nr, volatile unsigned long *addr)
{
	arch_set_bit(nr, addr);
}

static __always_inline void clear_bit(long nr, volatile unsigned long *addr)
{
	arch_clear_bit(nr, addr);
}

static __always_inline void __set_bit(long nr, volatile unsigned long *addr)
{
	arch___set_bit(nr, addr);
}

static __always_inline void __clear_bit(long nr, volatile unsigned long *addr)
{
	arch___clear_bit(nr, addr);
}

static __always_inline bool test_bit(long nr,
				     const volatile unsigned long *addr)
{
	return arch_test_bit(nr, addr);
}

static inline bool test_and_set_bit_lock(long nr, volatile unsigned long *addr)
{
	return arch_test_and_set_bit_lock(nr, addr);
}

static inline bool
clear_bit_unlock_is_negative_byte(long nr, volatile unsigned long *addr)
{
	return arch_clear_bit_unlock_is_negative_byte(nr, addr);
}

#endif
