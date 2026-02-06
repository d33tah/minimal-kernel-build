

#ifndef _LINUX_ATOMIC_FALLBACK_H
#define _LINUX_ATOMIC_FALLBACK_H

#include <linux/compiler.h>

/* arch_xchg ordering variants removed - zero callers */

#ifndef arch_cmpxchg_relaxed
#define arch_cmpxchg_acquire arch_cmpxchg
#define arch_cmpxchg_release arch_cmpxchg
#define arch_cmpxchg_relaxed arch_cmpxchg
#else  

#ifndef arch_cmpxchg_acquire
#define arch_cmpxchg_acquire(...) \
	__atomic_op_acquire(arch_cmpxchg, __VA_ARGS__)
#endif

#ifndef arch_cmpxchg_release
#define arch_cmpxchg_release(...) \
	__atomic_op_release(arch_cmpxchg, __VA_ARGS__)
#endif

#ifndef arch_cmpxchg
#define arch_cmpxchg(...) \
	__atomic_op_fence(arch_cmpxchg, __VA_ARGS__)
#endif

#endif  

/* arch_cmpxchg64 ordering variants removed - zero callers */

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

#ifndef arch_atomic_read_acquire
static __always_inline int
arch_atomic_read_acquire(const atomic_t *v)
{
	int ret;

	if (__native_word(atomic_t)) {
		ret = smp_load_acquire(&(v)->counter);
	} else {
		ret = arch_atomic_read(v);
		__atomic_acquire_fence();
	}

	return ret;
}
#define arch_atomic_read_acquire arch_atomic_read_acquire
#endif

#ifndef arch_atomic_add_return_relaxed
#define arch_atomic_add_return_acquire arch_atomic_add_return
#define arch_atomic_add_return_release arch_atomic_add_return
#define arch_atomic_add_return_relaxed arch_atomic_add_return
#else  

#ifndef arch_atomic_add_return_acquire
static __always_inline int
arch_atomic_add_return_acquire(int i, atomic_t *v)
{
	int ret = arch_atomic_add_return_relaxed(i, v);
	__atomic_acquire_fence();
	return ret;
}
#define arch_atomic_add_return_acquire arch_atomic_add_return_acquire
#endif

#ifndef arch_atomic_add_return_release
static __always_inline int
arch_atomic_add_return_release(int i, atomic_t *v)
{
	__atomic_release_fence();
	return arch_atomic_add_return_relaxed(i, v);
}
#define arch_atomic_add_return_release arch_atomic_add_return_release
#endif

#ifndef arch_atomic_add_return
static __always_inline int
arch_atomic_add_return(int i, atomic_t *v)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_add_return_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_add_return arch_atomic_add_return
#endif

#endif  

#ifndef arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_add_acquire arch_atomic_fetch_add
#define arch_atomic_fetch_add_release arch_atomic_fetch_add
#define arch_atomic_fetch_add_relaxed arch_atomic_fetch_add
#else  

#ifndef arch_atomic_fetch_add_release
static __always_inline int
arch_atomic_fetch_add_release(int i, atomic_t *v)
{
	__atomic_release_fence();
	return arch_atomic_fetch_add_relaxed(i, v);
}
#define arch_atomic_fetch_add_release arch_atomic_fetch_add_release
#endif

#ifndef arch_atomic_fetch_add
static __always_inline int
arch_atomic_fetch_add(int i, atomic_t *v)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_fetch_add_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_fetch_add arch_atomic_fetch_add
#endif

#endif  

#ifndef arch_atomic_sub_return_relaxed
#define arch_atomic_sub_return_acquire arch_atomic_sub_return
#define arch_atomic_sub_return_release arch_atomic_sub_return
#define arch_atomic_sub_return_relaxed arch_atomic_sub_return
#else  

#ifndef arch_atomic_sub_return
static __always_inline int
arch_atomic_sub_return(int i, atomic_t *v)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_sub_return_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_sub_return arch_atomic_sub_return
#endif

#endif  

#ifndef arch_atomic_fetch_sub_relaxed
#define arch_atomic_fetch_sub_acquire arch_atomic_fetch_sub
#define arch_atomic_fetch_sub_release arch_atomic_fetch_sub
#define arch_atomic_fetch_sub_relaxed arch_atomic_fetch_sub
#else  

#ifndef arch_atomic_fetch_sub_release
static __always_inline int
arch_atomic_fetch_sub_release(int i, atomic_t *v)
{
	__atomic_release_fence();
	return arch_atomic_fetch_sub_relaxed(i, v);
}
#define arch_atomic_fetch_sub_release arch_atomic_fetch_sub_release
#endif

#ifndef arch_atomic_fetch_sub
static __always_inline int
arch_atomic_fetch_sub(int i, atomic_t *v)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_fetch_sub_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_fetch_sub arch_atomic_fetch_sub
#endif

#endif  

#ifndef arch_atomic_inc
static __always_inline void
arch_atomic_inc(atomic_t *v)
{
	arch_atomic_add(1, v);
}
#define arch_atomic_inc arch_atomic_inc
#endif

#ifndef arch_atomic_inc_return_relaxed
#ifdef arch_atomic_inc_return
#define arch_atomic_inc_return_acquire arch_atomic_inc_return
#define arch_atomic_inc_return_release arch_atomic_inc_return
#define arch_atomic_inc_return_relaxed arch_atomic_inc_return
#endif  

#ifndef arch_atomic_inc_return
static __always_inline int
arch_atomic_inc_return(atomic_t *v)
{
	return arch_atomic_add_return(1, v);
}
#define arch_atomic_inc_return arch_atomic_inc_return
#endif

#ifndef arch_atomic_inc_return_acquire
static __always_inline int
arch_atomic_inc_return_acquire(atomic_t *v)
{
	return arch_atomic_add_return_acquire(1, v);
}
#define arch_atomic_inc_return_acquire arch_atomic_inc_return_acquire
#endif

#ifndef arch_atomic_inc_return_release
static __always_inline int
arch_atomic_inc_return_release(atomic_t *v)
{
	return arch_atomic_add_return_release(1, v);
}
#define arch_atomic_inc_return_release arch_atomic_inc_return_release
#endif

#ifndef arch_atomic_inc_return_relaxed
static __always_inline int
arch_atomic_inc_return_relaxed(atomic_t *v)
{
	return arch_atomic_add_return_relaxed(1, v);
}
#define arch_atomic_inc_return_relaxed arch_atomic_inc_return_relaxed
#endif

#else  

#ifndef arch_atomic_inc_return_acquire
static __always_inline int
arch_atomic_inc_return_acquire(atomic_t *v)
{
	int ret = arch_atomic_inc_return_relaxed(v);
	__atomic_acquire_fence();
	return ret;
}
#define arch_atomic_inc_return_acquire arch_atomic_inc_return_acquire
#endif

#ifndef arch_atomic_inc_return_release
static __always_inline int
arch_atomic_inc_return_release(atomic_t *v)
{
	__atomic_release_fence();
	return arch_atomic_inc_return_relaxed(v);
}
#define arch_atomic_inc_return_release arch_atomic_inc_return_release
#endif

#ifndef arch_atomic_inc_return
static __always_inline int
arch_atomic_inc_return(atomic_t *v)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_inc_return_relaxed(v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_inc_return arch_atomic_inc_return
#endif

#endif  

#ifndef arch_atomic_dec
static __always_inline void
arch_atomic_dec(atomic_t *v)
{
	arch_atomic_sub(1, v);
}
#define arch_atomic_dec arch_atomic_dec
#endif

#ifndef arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_and_acquire arch_atomic_fetch_and
#define arch_atomic_fetch_and_release arch_atomic_fetch_and
#define arch_atomic_fetch_and_relaxed arch_atomic_fetch_and
#else  

#ifndef arch_atomic_fetch_and
static __always_inline int
arch_atomic_fetch_and(int i, atomic_t *v)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_fetch_and_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_fetch_and arch_atomic_fetch_and
#endif

#endif  

#ifndef arch_atomic_andnot
static __always_inline void
arch_atomic_andnot(int i, atomic_t *v)
{
	arch_atomic_and(~i, v);
}
#define arch_atomic_andnot arch_atomic_andnot
#endif

#ifndef arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_or_acquire arch_atomic_fetch_or
#define arch_atomic_fetch_or_release arch_atomic_fetch_or
#define arch_atomic_fetch_or_relaxed arch_atomic_fetch_or
#else  

#ifndef arch_atomic_fetch_or
static __always_inline int
arch_atomic_fetch_or(int i, atomic_t *v)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_fetch_or_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_fetch_or arch_atomic_fetch_or
#endif

#endif  

#ifndef arch_atomic_fetch_xor_relaxed
#define arch_atomic_fetch_xor_acquire arch_atomic_fetch_xor
#define arch_atomic_fetch_xor_release arch_atomic_fetch_xor
#define arch_atomic_fetch_xor_relaxed arch_atomic_fetch_xor
#else  

#ifndef arch_atomic_fetch_xor
static __always_inline int
arch_atomic_fetch_xor(int i, atomic_t *v)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_fetch_xor_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_fetch_xor arch_atomic_fetch_xor
#endif

#endif  

#ifndef arch_atomic_xchg_relaxed
#define arch_atomic_xchg_acquire arch_atomic_xchg
#define arch_atomic_xchg_release arch_atomic_xchg
#define arch_atomic_xchg_relaxed arch_atomic_xchg
#else  

#ifndef arch_atomic_xchg
static __always_inline int
arch_atomic_xchg(atomic_t *v, int i)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_xchg_relaxed(v, i);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_xchg arch_atomic_xchg
#endif

#endif  

#ifndef arch_atomic_cmpxchg_relaxed
#define arch_atomic_cmpxchg_acquire arch_atomic_cmpxchg
#define arch_atomic_cmpxchg_release arch_atomic_cmpxchg
#define arch_atomic_cmpxchg_relaxed arch_atomic_cmpxchg
#else  

#ifndef arch_atomic_cmpxchg_acquire
static __always_inline int
arch_atomic_cmpxchg_acquire(atomic_t *v, int old, int new)
{
	int ret = arch_atomic_cmpxchg_relaxed(v, old, new);
	__atomic_acquire_fence();
	return ret;
}
#define arch_atomic_cmpxchg_acquire arch_atomic_cmpxchg_acquire
#endif

#ifndef arch_atomic_cmpxchg_release
static __always_inline int
arch_atomic_cmpxchg_release(atomic_t *v, int old, int new)
{
	__atomic_release_fence();
	return arch_atomic_cmpxchg_relaxed(v, old, new);
}
#define arch_atomic_cmpxchg_release arch_atomic_cmpxchg_release
#endif

#ifndef arch_atomic_cmpxchg
static __always_inline int
arch_atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_cmpxchg_relaxed(v, old, new);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_cmpxchg arch_atomic_cmpxchg
#endif

#endif  

#ifndef arch_atomic_try_cmpxchg_relaxed
#ifdef arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_acquire arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_release arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_relaxed arch_atomic_try_cmpxchg
#endif  

#ifndef arch_atomic_try_cmpxchg
static __always_inline bool
arch_atomic_try_cmpxchg(atomic_t *v, int *old, int new)
{
	int r, o = *old;
	r = arch_atomic_cmpxchg(v, o, new);
	if (unlikely(r != o))
		*old = r;
	return likely(r == o);
}
#define arch_atomic_try_cmpxchg arch_atomic_try_cmpxchg
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

#else  

#ifndef arch_atomic_try_cmpxchg_acquire
static __always_inline bool
arch_atomic_try_cmpxchg_acquire(atomic_t *v, int *old, int new)
{
	bool ret = arch_atomic_try_cmpxchg_relaxed(v, old, new);
	__atomic_acquire_fence();
	return ret;
}
#define arch_atomic_try_cmpxchg_acquire arch_atomic_try_cmpxchg_acquire
#endif

#ifndef arch_atomic_try_cmpxchg_release
static __always_inline bool
arch_atomic_try_cmpxchg_release(atomic_t *v, int *old, int new)
{
	__atomic_release_fence();
	return arch_atomic_try_cmpxchg_relaxed(v, old, new);
}
#define arch_atomic_try_cmpxchg_release arch_atomic_try_cmpxchg_release
#endif

#ifndef arch_atomic_try_cmpxchg
static __always_inline bool
arch_atomic_try_cmpxchg(atomic_t *v, int *old, int new)
{
	bool ret;
	__atomic_pre_full_fence();
	ret = arch_atomic_try_cmpxchg_relaxed(v, old, new);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic_try_cmpxchg arch_atomic_try_cmpxchg
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

#ifndef arch_atomic64_add_return_relaxed
#define arch_atomic64_add_return_acquire arch_atomic64_add_return
#define arch_atomic64_add_return_release arch_atomic64_add_return
#define arch_atomic64_add_return_relaxed arch_atomic64_add_return
#else  

#ifndef arch_atomic64_add_return
static __always_inline s64
arch_atomic64_add_return(s64 i, atomic64_t *v)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_add_return_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_add_return arch_atomic64_add_return
#endif

#endif  

#ifndef arch_atomic64_fetch_add_relaxed
#define arch_atomic64_fetch_add_acquire arch_atomic64_fetch_add
#define arch_atomic64_fetch_add_release arch_atomic64_fetch_add
#define arch_atomic64_fetch_add_relaxed arch_atomic64_fetch_add
#else  

#ifndef arch_atomic64_fetch_add
static __always_inline s64
arch_atomic64_fetch_add(s64 i, atomic64_t *v)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_fetch_add_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_fetch_add arch_atomic64_fetch_add
#endif

#endif  

#ifndef arch_atomic64_sub_return_relaxed
#define arch_atomic64_sub_return_acquire arch_atomic64_sub_return
#define arch_atomic64_sub_return_release arch_atomic64_sub_return
#define arch_atomic64_sub_return_relaxed arch_atomic64_sub_return
#else  

#ifndef arch_atomic64_sub_return
static __always_inline s64
arch_atomic64_sub_return(s64 i, atomic64_t *v)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_sub_return_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_sub_return arch_atomic64_sub_return
#endif

#endif  

#ifndef arch_atomic64_fetch_sub_relaxed
#define arch_atomic64_fetch_sub_acquire arch_atomic64_fetch_sub
#define arch_atomic64_fetch_sub_release arch_atomic64_fetch_sub
#define arch_atomic64_fetch_sub_relaxed arch_atomic64_fetch_sub
#else  

#ifndef arch_atomic64_fetch_sub
static __always_inline s64
arch_atomic64_fetch_sub(s64 i, atomic64_t *v)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_fetch_sub_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_fetch_sub arch_atomic64_fetch_sub
#endif

#endif  

#ifndef arch_atomic64_inc
static __always_inline void
arch_atomic64_inc(atomic64_t *v)
{
	arch_atomic64_add(1, v);
}
#define arch_atomic64_inc arch_atomic64_inc
#endif

#ifndef arch_atomic64_dec
static __always_inline void
arch_atomic64_dec(atomic64_t *v)
{
	arch_atomic64_sub(1, v);
}
#define arch_atomic64_dec arch_atomic64_dec
#endif

#ifndef arch_atomic64_fetch_and_relaxed
#define arch_atomic64_fetch_and_acquire arch_atomic64_fetch_and
#define arch_atomic64_fetch_and_release arch_atomic64_fetch_and
#define arch_atomic64_fetch_and_relaxed arch_atomic64_fetch_and
#else  

#ifndef arch_atomic64_fetch_and
static __always_inline s64
arch_atomic64_fetch_and(s64 i, atomic64_t *v)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_fetch_and_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_fetch_and arch_atomic64_fetch_and
#endif

#endif  

#ifndef arch_atomic64_fetch_or_relaxed
#define arch_atomic64_fetch_or_acquire arch_atomic64_fetch_or
#define arch_atomic64_fetch_or_release arch_atomic64_fetch_or
#define arch_atomic64_fetch_or_relaxed arch_atomic64_fetch_or
#else  

#ifndef arch_atomic64_fetch_or
static __always_inline s64
arch_atomic64_fetch_or(s64 i, atomic64_t *v)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_fetch_or_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_fetch_or arch_atomic64_fetch_or
#endif

#endif  

#ifndef arch_atomic64_fetch_xor_relaxed
#define arch_atomic64_fetch_xor_acquire arch_atomic64_fetch_xor
#define arch_atomic64_fetch_xor_release arch_atomic64_fetch_xor
#define arch_atomic64_fetch_xor_relaxed arch_atomic64_fetch_xor
#else  

#ifndef arch_atomic64_fetch_xor
static __always_inline s64
arch_atomic64_fetch_xor(s64 i, atomic64_t *v)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_fetch_xor_relaxed(i, v);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_fetch_xor arch_atomic64_fetch_xor
#endif

#endif  

#ifndef arch_atomic64_xchg_relaxed
#define arch_atomic64_xchg_acquire arch_atomic64_xchg
#define arch_atomic64_xchg_release arch_atomic64_xchg
#define arch_atomic64_xchg_relaxed arch_atomic64_xchg
#else  

#ifndef arch_atomic64_xchg
static __always_inline s64
arch_atomic64_xchg(atomic64_t *v, s64 i)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_xchg_relaxed(v, i);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_xchg arch_atomic64_xchg
#endif

#endif  

#ifndef arch_atomic64_cmpxchg_relaxed
#define arch_atomic64_cmpxchg_acquire arch_atomic64_cmpxchg
#define arch_atomic64_cmpxchg_release arch_atomic64_cmpxchg
#define arch_atomic64_cmpxchg_relaxed arch_atomic64_cmpxchg
#else  

#ifndef arch_atomic64_cmpxchg
static __always_inline s64
arch_atomic64_cmpxchg(atomic64_t *v, s64 old, s64 new)
{
	s64 ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_cmpxchg_relaxed(v, old, new);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_cmpxchg arch_atomic64_cmpxchg
#endif

#endif  

#ifndef arch_atomic64_try_cmpxchg_relaxed
#ifdef arch_atomic64_try_cmpxchg
#define arch_atomic64_try_cmpxchg_acquire arch_atomic64_try_cmpxchg
#define arch_atomic64_try_cmpxchg_release arch_atomic64_try_cmpxchg
#define arch_atomic64_try_cmpxchg_relaxed arch_atomic64_try_cmpxchg
#endif  

#ifndef arch_atomic64_try_cmpxchg
static __always_inline bool
arch_atomic64_try_cmpxchg(atomic64_t *v, s64 *old, s64 new)
{
	s64 r, o = *old;
	r = arch_atomic64_cmpxchg(v, o, new);
	if (unlikely(r != o))
		*old = r;
	return likely(r == o);
}
#define arch_atomic64_try_cmpxchg arch_atomic64_try_cmpxchg
#endif

#ifndef arch_atomic64_try_cmpxchg_acquire
static __always_inline bool
arch_atomic64_try_cmpxchg_acquire(atomic64_t *v, s64 *old, s64 new)
{
	s64 r, o = *old;
	r = arch_atomic64_cmpxchg_acquire(v, o, new);
	if (unlikely(r != o))
		*old = r;
	return likely(r == o);
}
#define arch_atomic64_try_cmpxchg_acquire arch_atomic64_try_cmpxchg_acquire
#endif

#ifndef arch_atomic64_try_cmpxchg_release
static __always_inline bool
arch_atomic64_try_cmpxchg_release(atomic64_t *v, s64 *old, s64 new)
{
	s64 r, o = *old;
	r = arch_atomic64_cmpxchg_release(v, o, new);
	if (unlikely(r != o))
		*old = r;
	return likely(r == o);
}
#define arch_atomic64_try_cmpxchg_release arch_atomic64_try_cmpxchg_release
#endif

#ifndef arch_atomic64_try_cmpxchg_relaxed
static __always_inline bool
arch_atomic64_try_cmpxchg_relaxed(atomic64_t *v, s64 *old, s64 new)
{
	s64 r, o = *old;
	r = arch_atomic64_cmpxchg_relaxed(v, o, new);
	if (unlikely(r != o))
		*old = r;
	return likely(r == o);
}
#define arch_atomic64_try_cmpxchg_relaxed arch_atomic64_try_cmpxchg_relaxed
#endif

#else  

#ifndef arch_atomic64_try_cmpxchg_acquire
static __always_inline bool
arch_atomic64_try_cmpxchg_acquire(atomic64_t *v, s64 *old, s64 new)
{
	bool ret = arch_atomic64_try_cmpxchg_relaxed(v, old, new);
	__atomic_acquire_fence();
	return ret;
}
#define arch_atomic64_try_cmpxchg_acquire arch_atomic64_try_cmpxchg_acquire
#endif

#ifndef arch_atomic64_try_cmpxchg_release
static __always_inline bool
arch_atomic64_try_cmpxchg_release(atomic64_t *v, s64 *old, s64 new)
{
	__atomic_release_fence();
	return arch_atomic64_try_cmpxchg_relaxed(v, old, new);
}
#define arch_atomic64_try_cmpxchg_release arch_atomic64_try_cmpxchg_release
#endif

#ifndef arch_atomic64_try_cmpxchg
static __always_inline bool
arch_atomic64_try_cmpxchg(atomic64_t *v, s64 *old, s64 new)
{
	bool ret;
	__atomic_pre_full_fence();
	ret = arch_atomic64_try_cmpxchg_relaxed(v, old, new);
	__atomic_post_full_fence();
	return ret;
}
#define arch_atomic64_try_cmpxchg arch_atomic64_try_cmpxchg
#endif

#endif  

#ifndef arch_atomic64_fetch_add_unless
static __always_inline s64
arch_atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u)
{
	s64 c = arch_atomic64_read(v);

	do {
		if (unlikely(c == u))
			break;
	} while (!arch_atomic64_try_cmpxchg(v, &c, c + a));

	return c;
}
#define arch_atomic64_fetch_add_unless arch_atomic64_fetch_add_unless
#endif

#ifndef arch_atomic64_add_unless
static __always_inline bool
arch_atomic64_add_unless(atomic64_t *v, s64 a, s64 u)
{
	return arch_atomic64_fetch_add_unless(v, a, u) != u;
}
#define arch_atomic64_add_unless arch_atomic64_add_unless
#endif

#ifndef arch_atomic64_inc_not_zero
static __always_inline bool
arch_atomic64_inc_not_zero(atomic64_t *v)
{
	return arch_atomic64_add_unless(v, 1, 0);
}
#define arch_atomic64_inc_not_zero arch_atomic64_inc_not_zero
#endif

#ifndef arch_atomic64_dec_if_positive
static __always_inline s64
arch_atomic64_dec_if_positive(atomic64_t *v)
{
	s64 dec, c = arch_atomic64_read(v);

	do {
		dec = c - 1;
		if (unlikely(dec < 0))
			break;
	} while (!arch_atomic64_try_cmpxchg(v, &c, dec));

	return dec;
}
#define arch_atomic64_dec_if_positive arch_atomic64_dec_if_positive
#endif

#endif  
