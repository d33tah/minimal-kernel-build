

#ifndef _LINUX_ATOMIC_INSTRUMENTED_H
#define _LINUX_ATOMIC_INSTRUMENTED_H

#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/instrumented.h>

static __always_inline int
atomic_read(const atomic_t *v)
{
	instrument_atomic_read(v, sizeof(*v));
	return arch_atomic_read(v);
}

/* atomic_read_acquire removed - unused */

static __always_inline void
atomic_set(atomic_t *v, int i)
{
	instrument_atomic_write(v, sizeof(*v));
	arch_atomic_set(v, i);
}

static __always_inline void
atomic_add(int i, atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	arch_atomic_add(i, v);
}

static __always_inline int
atomic_fetch_add_relaxed(int i, atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_fetch_add_relaxed(i, v);
}

/* atomic_sub removed - unused */

static __always_inline int
atomic_fetch_sub_release(int i, atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_fetch_sub_release(i, v);
}

static __always_inline void
atomic_inc(atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	arch_atomic_inc(v);
}

/* atomic_inc_return removed - unused */

static __always_inline void
atomic_dec(atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	arch_atomic_dec(v);
}

/* atomic_xchg removed - unused */

static __always_inline int
atomic_cmpxchg(atomic_t *v, int old, int new)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_cmpxchg(v, old, new);
}

static __always_inline bool
atomic_try_cmpxchg_release(atomic_t *v, int *old, int new)
{
	instrument_atomic_read_write(v, sizeof(*v));
	instrument_atomic_read_write(old, sizeof(*old));
	return arch_atomic_try_cmpxchg_release(v, old, new);
}

static __always_inline bool
atomic_try_cmpxchg_relaxed(atomic_t *v, int *old, int new)
{
	instrument_atomic_read_write(v, sizeof(*v));
	instrument_atomic_read_write(old, sizeof(*old));
	return arch_atomic_try_cmpxchg_relaxed(v, old, new);
}

static __always_inline bool
atomic_sub_and_test(int i, atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_sub_and_test(i, v);
}

static __always_inline bool
atomic_dec_and_test(atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_dec_and_test(v);
}

static __always_inline bool
atomic_inc_and_test(atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_inc_and_test(v);
}

static __always_inline bool
atomic_add_negative(int i, atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_add_negative(i, v);
}

static __always_inline bool
atomic_add_unless(atomic_t *v, int a, int u)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_add_unless(v, a, u);
}

/* atomic_inc_not_zero removed - unused */

static __always_inline bool
atomic_inc_unless_negative(atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_inc_unless_negative(v);
}

static __always_inline bool
atomic_dec_unless_positive(atomic_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_dec_unless_positive(v);
}

static __always_inline s64
atomic64_read(const atomic64_t *v)
{
	instrument_atomic_read(v, sizeof(*v));
	return arch_atomic64_read(v);
}

static __always_inline void
atomic64_set(atomic64_t *v, s64 i)
{
	instrument_atomic_write(v, sizeof(*v));
	arch_atomic64_set(v, i);
}

static __always_inline s64
atomic64_add_return(s64 i, atomic64_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic64_add_return(i, v);
}

static __always_inline s64
atomic64_inc_return(atomic64_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic64_inc_return(v);
}

static __always_inline long
atomic_long_read(const atomic_long_t *v)
{
	instrument_atomic_read(v, sizeof(*v));
	return arch_atomic_long_read(v);
}

static __always_inline void
atomic_long_set(atomic_long_t *v, long i)
{
	instrument_atomic_write(v, sizeof(*v));
	arch_atomic_long_set(v, i);
}

static __always_inline void
atomic_long_add(long i, atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	arch_atomic_long_add(i, v);
}

static __always_inline long
atomic_long_add_return(long i, atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_long_add_return(i, v);
}

/* atomic_long_add_return_acquire/release, fetch_add/release removed - unused */

static __always_inline void
atomic_long_sub(long i, atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	arch_atomic_long_sub(i, v);
}

static __always_inline long
atomic_long_sub_return(long i, atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_long_sub_return(i, v);
}

static __always_inline void
atomic_long_inc(atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	arch_atomic_long_inc(v);
}

static __always_inline void
atomic_long_dec(atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	arch_atomic_long_dec(v);
}

/* atomic_long_andnot, atomic_long_or removed - unused */

static __always_inline long
atomic_long_cmpxchg(atomic_long_t *v, long old, long new)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_long_cmpxchg(v, old, new);
}

static __always_inline bool
atomic_long_sub_and_test(long i, atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_long_sub_and_test(i, v);
}

static __always_inline bool
atomic_long_dec_and_test(atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_long_dec_and_test(v);
}

static __always_inline bool
atomic_long_inc_not_zero(atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_long_inc_not_zero(v);
}

static __always_inline long
atomic_long_dec_if_positive(atomic_long_t *v)
{
	instrument_atomic_read_write(v, sizeof(*v));
	return arch_atomic_long_dec_if_positive(v);
}

#define xchg(ptr, ...) \
({ \
	typeof(ptr) __ai_ptr = (ptr); \
	instrument_atomic_write(__ai_ptr, sizeof(*__ai_ptr)); \
	arch_xchg(__ai_ptr, __VA_ARGS__); \
})

#define cmpxchg(ptr, ...) \
({ \
	typeof(ptr) __ai_ptr = (ptr); \
	instrument_atomic_write(__ai_ptr, sizeof(*__ai_ptr)); \
	arch_cmpxchg(__ai_ptr, __VA_ARGS__); \
})

/* cmpxchg_relaxed removed - unused */

#define cmpxchg_double(ptr, ...) \
({ \
	typeof(ptr) __ai_ptr = (ptr); \
	instrument_atomic_write(__ai_ptr, 2 * sizeof(*__ai_ptr)); \
	arch_cmpxchg_double(__ai_ptr, __VA_ARGS__); \
})

#endif
