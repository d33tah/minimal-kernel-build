

#ifndef _LINUX_ATOMIC_FALLBACK_H
#define _LINUX_ATOMIC_FALLBACK_H

#define arch_atomic_add_return_acquire arch_atomic_add_return
#define arch_atomic_add_return_release arch_atomic_add_return

#define arch_atomic_fetch_add_release arch_atomic_fetch_add
#define arch_atomic_fetch_add_relaxed arch_atomic_fetch_add

#define arch_atomic_fetch_sub_release arch_atomic_fetch_sub

#define arch_atomic_try_cmpxchg_acquire arch_atomic_try_cmpxchg
#define arch_atomic_try_cmpxchg_release arch_atomic_try_cmpxchg

static __always_inline int arch_atomic_fetch_add_unless(atomic_t *v, int a,
							int u)
{
	int c = arch_atomic_read(v);

	do {
		if (unlikely(c == u))
			break;
	} while (!arch_atomic_try_cmpxchg(v, &c, c + a));

	return c;
}
#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless

static __always_inline bool arch_atomic_add_unless(atomic_t *v, int a, int u)
{
	return arch_atomic_fetch_add_unless(v, a, u) != u;
}
#define arch_atomic_add_unless arch_atomic_add_unless

static __always_inline bool arch_atomic_inc_unless_negative(atomic_t *v)
{
	int c = arch_atomic_read(v);

	do {
		if (unlikely(c < 0))
			return false;
	} while (!arch_atomic_try_cmpxchg(v, &c, c + 1));

	return true;
}
#define arch_atomic_inc_unless_negative arch_atomic_inc_unless_negative

static __always_inline bool arch_atomic_dec_unless_positive(atomic_t *v)
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
