

#ifndef _LINUX_ATOMIC_LONG_H
#define _LINUX_ATOMIC_LONG_H

#include <linux/compiler.h>
#include <asm/types.h>

typedef atomic_t atomic_long_t;
#define ATOMIC_LONG_INIT(i)		ATOMIC_INIT(i)

static __always_inline long
arch_atomic_long_read(const atomic_long_t *v)
{
	return arch_atomic_read(v);
}

static __always_inline void
arch_atomic_long_set(atomic_long_t *v, long i)
{
	arch_atomic_set(v, i);
}

static __always_inline void
arch_atomic_long_add(long i, atomic_long_t *v)
{
	arch_atomic_add(i, v);
}

static __always_inline long
arch_atomic_long_add_return(long i, atomic_long_t *v)
{
	return arch_atomic_add_return(i, v);
}

/* arch_atomic_long_add_return_acquire/release, fetch_add/release removed - unused */

static __always_inline void
arch_atomic_long_sub(long i, atomic_long_t *v)
{
	arch_atomic_sub(i, v);
}

static __always_inline long
arch_atomic_long_sub_return(long i, atomic_long_t *v)
{
	return arch_atomic_sub_return(i, v);
}

static __always_inline void
arch_atomic_long_inc(atomic_long_t *v)
{
	arch_atomic_inc(v);
}

static __always_inline void
arch_atomic_long_dec(atomic_long_t *v)
{
	arch_atomic_dec(v);
}

/* arch_atomic_long_andnot, arch_atomic_long_or removed - unused */

static __always_inline long
arch_atomic_long_cmpxchg(atomic_long_t *v, long old, long new)
{
	return arch_atomic_cmpxchg(v, old, new);
}

static __always_inline bool
arch_atomic_long_sub_and_test(long i, atomic_long_t *v)
{
	return arch_atomic_sub_and_test(i, v);
}

static __always_inline bool
arch_atomic_long_dec_and_test(atomic_long_t *v)
{
	return arch_atomic_dec_and_test(v);
}

static __always_inline bool
arch_atomic_long_inc_not_zero(atomic_long_t *v)
{
	return arch_atomic_inc_not_zero(v);
}

static __always_inline long
arch_atomic_long_dec_if_positive(atomic_long_t *v)
{
	return arch_atomic_dec_if_positive(v);
}

#endif
