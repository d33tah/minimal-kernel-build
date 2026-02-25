#ifndef _LINUX_ATOMIC_H
#define _LINUX_ATOMIC_H

#include <asm/atomic.h>
#include <asm/barrier.h>

/* Fallback functions for operations not provided by arch */
#include <linux/compiler.h>

#ifndef try_cmpxchg
#define try_cmpxchg(_ptr, _oldp, _new) \
({ \
	typeof(*(_ptr)) *___op = (_oldp), ___o = *___op, ___r; \
	___r = cmpxchg((_ptr), ___o, (_new)); \
	if (unlikely(___r != ___o)) \
		*___op = ___r; \
	likely(___r == ___o); \
})
#endif

#ifndef atomic_fetch_add_relaxed
#define atomic_fetch_add_relaxed atomic_fetch_add
#endif

#ifndef atomic_fetch_sub_relaxed
#define atomic_fetch_sub_release atomic_fetch_sub
#define atomic_fetch_sub_relaxed atomic_fetch_sub
#endif

#ifndef atomic_try_cmpxchg_relaxed
#ifdef atomic_try_cmpxchg
#define atomic_try_cmpxchg_relaxed atomic_try_cmpxchg
#endif
#endif

#ifndef atomic_fetch_add_unless
static __always_inline int
atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int c = atomic_read(v);

	do {
		if (unlikely(c == u))
			break;
	} while (!atomic_try_cmpxchg(v, &c, c + a));

	return c;
}
#define atomic_fetch_add_unless atomic_fetch_add_unless
#endif

#ifndef atomic_add_unless
static __always_inline bool
atomic_add_unless(atomic_t *v, int a, int u)
{
	return atomic_fetch_add_unless(v, a, u) != u;
}
#define atomic_add_unless atomic_add_unless
#endif

#ifndef atomic_inc_unless_negative
static __always_inline bool
atomic_inc_unless_negative(atomic_t *v)
{
	int c = atomic_read(v);

	do {
		if (unlikely(c < 0))
			return false;
	} while (!atomic_try_cmpxchg(v, &c, c + 1));

	return true;
}
#define atomic_inc_unless_negative atomic_inc_unless_negative
#endif

#ifndef atomic_dec_unless_positive
static __always_inline bool
atomic_dec_unless_positive(atomic_t *v)
{
	int c = atomic_read(v);

	do {
		if (unlikely(c > 0))
			return false;
	} while (!atomic_try_cmpxchg(v, &c, c - 1));

	return true;
}
#define atomic_dec_unless_positive atomic_dec_unless_positive
#endif

/* atomic_long - on 32-bit, maps to atomic_t */
typedef atomic_t atomic_long_t;
#define ATOMIC_LONG_INIT(i)		ATOMIC_INIT(i)

static __always_inline long
atomic_long_read(const atomic_long_t *v)
{
	return atomic_read(v);
}

static __always_inline void
atomic_long_set(atomic_long_t *v, long i)
{
	atomic_set(v, i);
}

static __always_inline void
atomic_long_add(long i, atomic_long_t *v)
{
	atomic_add(i, v);
}

static __always_inline void
atomic_long_sub(long i, atomic_long_t *v)
{
	atomic_sub(i, v);
}

static __always_inline void
atomic_long_inc(atomic_long_t *v)
{
	atomic_inc(v);
}

static __always_inline void
atomic_long_dec(atomic_long_t *v)
{
	atomic_dec(v);
}

static __always_inline bool
atomic_long_dec_and_test(atomic_long_t *v)
{
	return atomic_dec_and_test(v);
}


#endif
