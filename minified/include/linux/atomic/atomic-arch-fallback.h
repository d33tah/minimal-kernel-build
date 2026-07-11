

#ifndef _LINUX_ATOMIC_FALLBACK_H
#define _LINUX_ATOMIC_FALLBACK_H

#include <linux/compiler.h>

#ifndef arch_xchg_relaxed
#define arch_xchg_relaxed arch_xchg
#endif

#ifndef arch_cmpxchg_relaxed
#define arch_cmpxchg_acquire arch_cmpxchg
#define arch_cmpxchg_release arch_cmpxchg
#define arch_cmpxchg_relaxed arch_cmpxchg
#endif

#ifndef arch_cmpxchg64_relaxed
#define arch_cmpxchg64_acquire arch_cmpxchg64
#define arch_cmpxchg64_release arch_cmpxchg64
#define arch_cmpxchg64_relaxed arch_cmpxchg64
#endif

#ifndef arch_try_cmpxchg_relaxed
#ifdef arch_try_cmpxchg
#define arch_try_cmpxchg_acquire arch_try_cmpxchg
#define arch_try_cmpxchg_release arch_try_cmpxchg
#define arch_try_cmpxchg_relaxed arch_try_cmpxchg
#endif  

#ifndef arch_try_cmpxchg_acquire
#define arch_try_cmpxchg_acquire(_ptr, _oldp, _new) \
({ \
	typeof(*(_ptr)) *___op = (_oldp), ___o = *___op, ___r; \
	___r = arch_cmpxchg_acquire((_ptr), ___o, (_new)); \
	if (unlikely(___r != ___o)) \
		*___op = ___r; \
	likely(___r == ___o); \
})
#endif  

#ifndef arch_try_cmpxchg_release
#define arch_try_cmpxchg_release(_ptr, _oldp, _new) \
({ \
	typeof(*(_ptr)) *___op = (_oldp), ___o = *___op, ___r; \
	___r = arch_cmpxchg_release((_ptr), ___o, (_new)); \
	if (unlikely(___r != ___o)) \
		*___op = ___r; \
	likely(___r == ___o); \
})
#endif  

#ifndef arch_try_cmpxchg_relaxed
#define arch_try_cmpxchg_relaxed(_ptr, _oldp, _new) \
({ \
	typeof(*(_ptr)) *___op = (_oldp), ___o = *___op, ___r; \
	___r = arch_cmpxchg_relaxed((_ptr), ___o, (_new)); \
	if (unlikely(___r != ___o)) \
		*___op = ___r; \
	likely(___r == ___o); \
})
#endif

#endif

#ifndef arch_try_cmpxchg64_relaxed
#ifdef arch_try_cmpxchg64
#define arch_try_cmpxchg64_acquire arch_try_cmpxchg64
#define arch_try_cmpxchg64_release arch_try_cmpxchg64
#define arch_try_cmpxchg64_relaxed arch_try_cmpxchg64
#endif  

#ifndef arch_try_cmpxchg64_acquire
#define arch_try_cmpxchg64_acquire(_ptr, _oldp, _new) \
({ \
	typeof(*(_ptr)) *___op = (_oldp), ___o = *___op, ___r; \
	___r = arch_cmpxchg64_acquire((_ptr), ___o, (_new)); \
	if (unlikely(___r != ___o)) \
		*___op = ___r; \
	likely(___r == ___o); \
})
#endif  

#ifndef arch_try_cmpxchg64_release
#define arch_try_cmpxchg64_release(_ptr, _oldp, _new) \
({ \
	typeof(*(_ptr)) *___op = (_oldp), ___o = *___op, ___r; \
	___r = arch_cmpxchg64_release((_ptr), ___o, (_new)); \
	if (unlikely(___r != ___o)) \
		*___op = ___r; \
	likely(___r == ___o); \
})
#endif  

#ifndef arch_try_cmpxchg64_relaxed
#define arch_try_cmpxchg64_relaxed(_ptr, _oldp, _new) \
({ \
	typeof(*(_ptr)) *___op = (_oldp), ___o = *___op, ___r; \
	___r = arch_cmpxchg64_relaxed((_ptr), ___o, (_new)); \
	if (unlikely(___r != ___o)) \
		*___op = ___r; \
	likely(___r == ___o); \
})
#endif

#endif

#ifndef arch_atomic_read_acquire
#endif

#ifndef arch_atomic_set_release
#endif

#ifndef arch_atomic_add_return_relaxed
#define arch_atomic_add_return_acquire arch_atomic_add_return
#define arch_atomic_add_return_release arch_atomic_add_return
#endif

#ifndef arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_add_release arch_atomic_fetch_add
#define arch_atomic_fetch_add_relaxed arch_atomic_fetch_add
#endif

#ifndef arch_atomic_sub_return_relaxed
#endif

#ifndef arch_atomic_fetch_sub_relaxed
#define arch_atomic_fetch_sub_release arch_atomic_fetch_sub
#endif


#ifndef arch_atomic_inc_return_relaxed
#ifdef arch_atomic_inc_return
#endif  

#ifndef arch_atomic_inc_return
#endif

#ifndef arch_atomic_inc_return_acquire
#endif

#ifndef arch_atomic_inc_return_release
#endif

#ifndef arch_atomic_inc_return_relaxed
#endif

#endif

#ifndef arch_atomic_fetch_inc_relaxed
#ifdef arch_atomic_fetch_inc
#endif  

#ifndef arch_atomic_fetch_inc
#endif

#ifndef arch_atomic_fetch_inc_acquire
#endif

#ifndef arch_atomic_fetch_inc_release
#endif

#ifndef arch_atomic_fetch_inc_relaxed
#endif

#endif


#ifndef arch_atomic_dec_return_relaxed
#ifdef arch_atomic_dec_return
#endif  

#ifndef arch_atomic_dec_return
#endif

#ifndef arch_atomic_dec_return_acquire
#endif

#ifndef arch_atomic_dec_return_release
#endif

#ifndef arch_atomic_dec_return_relaxed
#endif

#endif

#ifndef arch_atomic_fetch_dec_relaxed
#ifdef arch_atomic_fetch_dec
#endif  

#ifndef arch_atomic_fetch_dec
#endif

#ifndef arch_atomic_fetch_dec_acquire
#endif

#ifndef arch_atomic_fetch_dec_release
#endif

#ifndef arch_atomic_fetch_dec_relaxed
#endif

#endif

#ifndef arch_atomic_fetch_and_relaxed
#endif

#ifndef arch_atomic_andnot
#endif

#ifndef arch_atomic_fetch_andnot_relaxed
#ifdef arch_atomic_fetch_andnot
#endif  

#ifndef arch_atomic_fetch_andnot
#endif

#ifndef arch_atomic_fetch_andnot_acquire
#endif

#ifndef arch_atomic_fetch_andnot_release
#endif

#ifndef arch_atomic_fetch_andnot_relaxed
#endif

#endif

#ifndef arch_atomic_cmpxchg_relaxed
#define arch_atomic_cmpxchg_acquire arch_atomic_cmpxchg
#define arch_atomic_cmpxchg_release arch_atomic_cmpxchg
#define arch_atomic_cmpxchg_relaxed arch_atomic_cmpxchg
#endif

#ifndef arch_atomic_try_cmpxchg_relaxed
#ifdef arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_acquire arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_release arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_relaxed arch_atomic_try_cmpxchg
#endif  

#ifndef arch_atomic_try_cmpxchg_acquire
static __always_inline bool
arch_atomic_try_cmpxchg_acquire(atomic_t *v, int *old, int new)
{
	int r, o = *old;
	r = arch_atomic_cmpxchg_acquire(v, o, new);
	if (unlikely(r != o))
		*old = r;
	return likely(r == o);
}
#define arch_atomic_try_cmpxchg_acquire arch_atomic_try_cmpxchg_acquire
#endif

#ifndef arch_atomic_try_cmpxchg_release
static __always_inline bool
arch_atomic_try_cmpxchg_release(atomic_t *v, int *old, int new)
{
	int r, o = *old;
	r = arch_atomic_cmpxchg_release(v, o, new);
	if (unlikely(r != o))
		*old = r;
	return likely(r == o);
}
#define arch_atomic_try_cmpxchg_release arch_atomic_try_cmpxchg_release
#endif

#ifndef arch_atomic_try_cmpxchg_relaxed
static __always_inline bool
arch_atomic_try_cmpxchg_relaxed(atomic_t *v, int *old, int new)
{
	int r, o = *old;
	r = arch_atomic_cmpxchg_relaxed(v, o, new);
	if (unlikely(r != o))
		*old = r;
	return likely(r == o);
}
#define arch_atomic_try_cmpxchg_relaxed arch_atomic_try_cmpxchg_relaxed
#endif

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


#ifndef arch_atomic64_read_acquire
#endif

#ifndef arch_atomic64_set_release
#endif

#ifndef arch_atomic64_add_return_relaxed
#endif

#ifndef arch_atomic64_fetch_add_relaxed
#endif

#ifndef arch_atomic64_sub_return_relaxed
#endif

#ifndef arch_atomic64_fetch_sub_relaxed
#endif


#ifndef arch_atomic64_inc_return_relaxed
#ifdef arch_atomic64_inc_return
#endif  

#ifndef arch_atomic64_inc_return_acquire
#endif

#ifndef arch_atomic64_inc_return_release
#endif

#ifndef arch_atomic64_inc_return_relaxed
#endif

#endif

#ifndef arch_atomic64_fetch_inc_relaxed
#ifdef arch_atomic64_fetch_inc
#endif  

#ifndef arch_atomic64_fetch_inc
#endif

#ifndef arch_atomic64_fetch_inc_acquire
#endif

#ifndef arch_atomic64_fetch_inc_release
#endif

#ifndef arch_atomic64_fetch_inc_relaxed
#endif

#endif


#ifndef arch_atomic64_dec_return_relaxed
#ifdef arch_atomic64_dec_return
#endif  

#ifndef arch_atomic64_dec_return_acquire
#endif

#ifndef arch_atomic64_dec_return_release
#endif

#ifndef arch_atomic64_dec_return_relaxed
#endif

#endif

#ifndef arch_atomic64_fetch_dec_relaxed
#ifdef arch_atomic64_fetch_dec
#endif  

#ifndef arch_atomic64_fetch_dec
#endif

#ifndef arch_atomic64_fetch_dec_acquire
#endif

#ifndef arch_atomic64_fetch_dec_release
#endif

#ifndef arch_atomic64_fetch_dec_relaxed
#endif

#endif

#ifndef arch_atomic64_fetch_and_relaxed
#endif

#ifndef arch_atomic64_andnot
#endif

#ifndef arch_atomic64_fetch_andnot_relaxed
#ifdef arch_atomic64_fetch_andnot
#endif  

#ifndef arch_atomic64_fetch_andnot
#endif

#ifndef arch_atomic64_fetch_andnot_acquire
#endif

#ifndef arch_atomic64_fetch_andnot_release
#endif

#ifndef arch_atomic64_fetch_andnot_relaxed
#endif

#endif

#ifndef arch_atomic64_cmpxchg_relaxed
#endif

#ifndef arch_atomic64_try_cmpxchg_relaxed
#ifdef arch_atomic64_try_cmpxchg
#endif  

#ifndef arch_atomic64_try_cmpxchg
#endif

#ifndef arch_atomic64_try_cmpxchg_acquire
#endif

#ifndef arch_atomic64_try_cmpxchg_release
#endif

#ifndef arch_atomic64_try_cmpxchg_relaxed
#endif

#endif

#ifndef arch_atomic64_sub_and_test
#endif

#ifndef arch_atomic64_dec_and_test
#endif

#ifndef arch_atomic64_inc_and_test
#endif

#ifndef arch_atomic64_add_negative
#endif

#ifndef arch_atomic64_fetch_add_unless
#endif



#ifndef arch_atomic64_inc_unless_negative
#endif

#ifndef arch_atomic64_dec_unless_positive
#endif


#endif  
