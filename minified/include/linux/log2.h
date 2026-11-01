/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Integer base 2 logarithm calculation
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_LOG2_H
#define _LINUX_LOG2_H

#include <linux/bitops.h>

static __always_inline __attribute__((const)) int __ilog2_u32(u32 n)
{
	return fls(n) - 1;
}

static __always_inline __attribute__((const)) int __ilog2_u64(u64 n)
{
	return fls64(n) - 1;
}

static inline __attribute__((const)) bool is_power_of_2(unsigned long n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}

static inline __attribute__((const)) unsigned long
__roundup_pow_of_two(unsigned long n)
{
	return 1UL << fls_long(n - 1);
}

/**
 * ilog2 - log base 2 of 32-bit or a 64-bit unsigned value
 * @n: parameter
 *
 * constant-capable log of base 2 calculation
 * - this can be used to initialise global variables from constant data, hence
 * the massive ternary operator construction
 *
 * selects the appropriately-sized optimised version depending on sizeof(n)
 */
#define ilog2(n)                                                             \
	(__builtin_constant_p(n) ? ((n) < 2 ? 0 : 63 - __builtin_clzll(n)) : \
	 (sizeof(n) <= 4)	 ? __ilog2_u32(n) :                          \
				   __ilog2_u64(n))

/**
 * roundup_pow_of_two - round the given value up to nearest power of two
 * @n: parameter
 *
 * round the given value up to the nearest power of two
 * - the result is undefined when n == 0
 * - this can be used to initialise global variables from constant data
 */
#define roundup_pow_of_two(n)                                       \
	(__builtin_constant_p(n) ?                                  \
		 (((n) == 1) ? 1 : (1UL << (ilog2((n) - 1) + 1))) : \
		 __roundup_pow_of_two(n))

#endif /* _LINUX_LOG2_H */
