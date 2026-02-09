#ifndef _LINUX_ATOMIC_H
#define _LINUX_ATOMIC_H

#include <asm/atomic.h>
#include <asm/barrier.h>

#ifndef __atomic_acquire_fence
#define __atomic_acquire_fence		smp_mb__after_atomic
#endif

#ifndef __atomic_release_fence
#define __atomic_release_fence		smp_mb__before_atomic
#endif

#ifndef __atomic_pre_full_fence
#define __atomic_pre_full_fence		smp_mb__before_atomic
#endif

#ifndef __atomic_post_full_fence
#define __atomic_post_full_fence	smp_mb__after_atomic
#endif

#define __atomic_op_acquire(op, args...)				\
({									\
	typeof(op##_relaxed(args)) __ret  = op##_relaxed(args);		\
	__atomic_acquire_fence();					\
	__ret;								\
})

#define __atomic_op_release(op, args...)				\
({									\
	__atomic_release_fence();					\
	op##_relaxed(args);						\
})

#define __atomic_op_fence(op, args...)					\
({									\
	typeof(op##_relaxed(args)) __ret;				\
	__atomic_pre_full_fence();					\
	__ret = op##_relaxed(args);					\
	__atomic_post_full_fence();					\
	__ret;								\
})

/* Inlined from linux/atomic/atomic-arch-fallback.h */
#include <linux/compiler.h>

#ifndef arch_try_cmpxchg
#define arch_try_cmpxchg(_ptr, _oldp, _new) \
({ \
	typeof(*(_ptr)) *___op = (_oldp), ___o = *___op, ___r; \
	___r = arch_cmpxchg((_ptr), ___o, (_new)); \
	if (unlikely(___r != ___o)) \
		*___op = ___r; \
	likely(___r == ___o); \
})
#endif

/* arch_try_cmpxchg64 and all ordering variants removed - zero callers */

/* arch_atomic_read_acquire removed - unused */

#ifndef arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_add_acquire arch_atomic_fetch_add
#define arch_atomic_fetch_add_release arch_atomic_fetch_add
#define arch_atomic_fetch_add_relaxed arch_atomic_fetch_add
#endif

#ifndef arch_atomic_fetch_sub_relaxed
#define arch_atomic_fetch_sub_acquire arch_atomic_fetch_sub
#define arch_atomic_fetch_sub_release arch_atomic_fetch_sub
#define arch_atomic_fetch_sub_relaxed arch_atomic_fetch_sub
#endif

#ifndef arch_atomic_inc
static __always_inline void
arch_atomic_inc(atomic_t *v)
{
	arch_atomic_add(1, v);
}
#define arch_atomic_inc arch_atomic_inc
#endif

/* arch_atomic_inc_return removed - unused */

#ifndef arch_atomic_dec
static __always_inline void
arch_atomic_dec(atomic_t *v)
{
	arch_atomic_sub(1, v);
}
#define arch_atomic_dec arch_atomic_dec
#endif

/* arch_atomic_fetch_and ordering variants removed - zero callers */

/* x86 provides arch_atomic_try_cmpxchg natively - aliases for ordering variants */
#ifndef arch_atomic_try_cmpxchg_relaxed
#ifdef arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_acquire arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_release arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_relaxed arch_atomic_try_cmpxchg
#endif
#endif

#ifndef arch_atomic_sub_and_test
static __always_inline bool
arch_atomic_sub_and_test(int i, atomic_t *v)
{
	return arch_atomic_sub_return(i, v) == 0;
}
#define arch_atomic_sub_and_test arch_atomic_sub_and_test
#endif

#ifndef arch_atomic_dec_and_test
static __always_inline bool
arch_atomic_dec_and_test(atomic_t *v)
{
	return arch_atomic_dec_return(v) == 0;
}
#define arch_atomic_dec_and_test arch_atomic_dec_and_test
#endif

#ifndef arch_atomic_inc_and_test
static __always_inline bool
arch_atomic_inc_and_test(atomic_t *v)
{
	return arch_atomic_inc_return(v) == 0;
}
#define arch_atomic_inc_and_test arch_atomic_inc_and_test
#endif

#ifndef arch_atomic_add_negative
static __always_inline bool
arch_atomic_add_negative(int i, atomic_t *v)
{
	return arch_atomic_add_return(i, v) < 0;
}
#define arch_atomic_add_negative arch_atomic_add_negative
#endif

#ifndef arch_atomic_fetch_add_unless
static __always_inline int
arch_atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int c = arch_atomic_read(v);

	do {
		if (unlikely(c == u))
			break;
	} while (!arch_atomic_try_cmpxchg(v, &c, c + a));

	return c;
}
#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless
#endif

#ifndef arch_atomic_add_unless
static __always_inline bool
arch_atomic_add_unless(atomic_t *v, int a, int u)
{
	return arch_atomic_fetch_add_unless(v, a, u) != u;
}
#define arch_atomic_add_unless arch_atomic_add_unless
#endif

#ifndef arch_atomic_inc_not_zero
static __always_inline bool
arch_atomic_inc_not_zero(atomic_t *v)
{
	return arch_atomic_add_unless(v, 1, 0);
}
#define arch_atomic_inc_not_zero arch_atomic_inc_not_zero
#endif

#ifndef arch_atomic_inc_unless_negative
static __always_inline bool
arch_atomic_inc_unless_negative(atomic_t *v)
{
	int c = arch_atomic_read(v);

	do {
		if (unlikely(c < 0))
			return false;
	} while (!arch_atomic_try_cmpxchg(v, &c, c + 1));

	return true;
}
#define arch_atomic_inc_unless_negative arch_atomic_inc_unless_negative
#endif

#ifndef arch_atomic_dec_unless_positive
static __always_inline bool
arch_atomic_dec_unless_positive(atomic_t *v)
{
	int c = arch_atomic_read(v);

	do {
		if (unlikely(c > 0))
			return false;
	} while (!arch_atomic_try_cmpxchg(v, &c, c - 1));

	return true;
}
#define arch_atomic_dec_unless_positive arch_atomic_dec_unless_positive
#endif

#ifndef arch_atomic_dec_if_positive
static __always_inline int
arch_atomic_dec_if_positive(atomic_t *v)
{
	int dec, c = arch_atomic_read(v);

	do {
		dec = c - 1;
		if (unlikely(dec < 0))
			break;
	} while (!arch_atomic_try_cmpxchg(v, &c, dec));

	return dec;
}
#define arch_atomic_dec_if_positive arch_atomic_dec_if_positive
#endif

/* All arch_atomic64_* ordering fallbacks and derived functions removed -
   x86-32 provides native implementations in asm/atomic64_32.h.
   Only 4 arch_atomic64 functions are used: read, set, add_return, inc_return. */
/* Inlined from linux/atomic/atomic-long.h */
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
/* Inlined from linux/atomic/atomic-instrumented.h */
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
