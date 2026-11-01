/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ATOMIC_H
#define _ASM_X86_ATOMIC_H

#include <asm/cmpxchg.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/**
 * arch_atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
static __always_inline int arch_atomic_read(const atomic_t *v)
{
	/*
	 * Note for KASAN: we deliberately don't use READ_ONCE_NOCHECK() here,
	 * it's non-inlined function that increases binary size and stack usage.
	 */
	return __READ_ONCE((v)->counter);
}

/**
 * arch_atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
static __always_inline void arch_atomic_set(atomic_t *v, int i)
{
	__WRITE_ONCE(v->counter, i);
}

/**
 * arch_atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.
 */
static __always_inline void arch_atomic_add(int i, atomic_t *v)
{
	asm volatile("addl %1,%0" : "+m"(v->counter) : "ir"(i) : "memory");
}

/**
 * arch_atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
static __always_inline void arch_atomic_inc(atomic_t *v)
{
	asm volatile("incl %0" : "+m"(v->counter)::"memory");
}

/**
 * arch_atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
static __always_inline void arch_atomic_dec(atomic_t *v)
{
	asm volatile("decl %0" : "+m"(v->counter)::"memory");
}

/**
 * arch_atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static __always_inline bool arch_atomic_dec_and_test(atomic_t *v)
{
	return GEN_UNARY_RMWcc("decl", v->counter, e);
}

/**
 * arch_atomic_add_return - add integer and return
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v and returns @i + @v
 */
static __always_inline int arch_atomic_add_return(int i, atomic_t *v)
{
	return i + xadd(&v->counter, i);
}

static __always_inline int arch_atomic_fetch_add(int i, atomic_t *v)
{
	return xadd(&v->counter, i);
}

static __always_inline int arch_atomic_fetch_sub(int i, atomic_t *v)
{
	return xadd(&v->counter, -i);
}

static __always_inline bool arch_atomic_try_cmpxchg(atomic_t *v, int *old,
						    int new)
{
	return arch_try_cmpxchg(&v->counter, old, new);
}
#define arch_atomic_try_cmpxchg arch_atomic_try_cmpxchg

#endif
