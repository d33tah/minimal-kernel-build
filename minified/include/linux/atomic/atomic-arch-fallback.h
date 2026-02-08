

#ifndef _LINUX_ATOMIC_FALLBACK_H
#define _LINUX_ATOMIC_FALLBACK_H

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

#endif
