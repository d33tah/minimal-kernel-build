#ifndef _ASM_X86_ATOMIC_H
#define _ASM_X86_ATOMIC_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/alternative.h>
#include <asm/cpufeatures.h>

/* Only 4-byte (L) case needed - all callers use int/unsigned/pointer */
#define __xchg_op(ptr, arg, op, lock)					\
	({								\
	        __typeof__ (*(ptr)) __ret = (arg);			\
		asm volatile (lock #op "l %0, %1\n"			\
			      : "+r" (__ret), "+m" (*(ptr))		\
			      : : "memory", "cc");			\
		__ret;							\
	})

#define xchg(ptr, v)	__xchg_op((ptr), (v), xchg, "")

#define __raw_cmpxchg(ptr, old, new, size, lock)			\
({									\
	__typeof__(*(ptr)) __ret;					\
	__typeof__(*(ptr)) __old = (old);				\
	__typeof__(*(ptr)) __new = (new);				\
	volatile u32 *__ptr = (volatile u32 *)(ptr);			\
	asm volatile(lock "cmpxchgl %2,%1"				\
		     : "=a" (__ret), "+m" (*__ptr)			\
		     : "r" (__new), "0" (__old)				\
		     : "memory");					\
	__ret;								\
})

#define __cmpxchg(ptr, old, new, size)					\
	__raw_cmpxchg((ptr), (old), (new), (size), LOCK_PREFIX)


#define cmpxchg(ptr, old, new)						\
	__cmpxchg(ptr, old, new, sizeof(*(ptr)))

#define __raw_try_cmpxchg(_ptr, _pold, _new, size, lock)		\
({									\
	bool success;							\
	__typeof__(_ptr) _old = (__typeof__(_ptr))(_pold);		\
	__typeof__(*(_ptr)) __old = *_old;				\
	__typeof__(*(_ptr)) __new = (_new);				\
	volatile u32 *__ptr = (volatile u32 *)(_ptr);			\
	asm volatile(lock "cmpxchgl %[new], %[ptr]"			\
		     CC_SET(z)						\
		     : CC_OUT(z) (success),				\
		       [ptr] "+m" (*__ptr),				\
		       [old] "+a" (__old)				\
		     : [new] "r" (__new)				\
		     : "memory");					\
	if (unlikely(!success))						\
		*_old = __old;						\
	likely(success);						\
})

#define __try_cmpxchg(ptr, pold, new, size)				\
	__raw_try_cmpxchg((ptr), (pold), (new), (size), LOCK_PREFIX)

#define try_cmpxchg(ptr, pold, new) 					\
	__try_cmpxchg((ptr), (pold), (new), sizeof(*(ptr)))

#define __xadd(ptr, inc, lock)	__xchg_op((ptr), (inc), xadd, lock)
#define xadd(ptr, inc)		__xadd((ptr), (inc), LOCK_PREFIX)

/* end cmpxchg.h inline */
#include <asm/rmwcc.h>
#include <asm/barrier.h>



static __always_inline int atomic_read(const atomic_t *v)
{
	return __READ_ONCE((v)->counter);
}

static __always_inline void atomic_set(atomic_t *v, int i)
{
	__WRITE_ONCE(v->counter, i);
}

static __always_inline void atomic_add(int i, atomic_t *v)
{
	asm volatile(LOCK_PREFIX "addl %1,%0"
		     : "+m" (v->counter)
		     : "ir" (i) : "memory");
}

static __always_inline void atomic_sub(int i, atomic_t *v)
{
	asm volatile(LOCK_PREFIX "subl %1,%0"
		     : "+m" (v->counter)
		     : "ir" (i) : "memory");
}

static __always_inline bool atomic_sub_and_test(int i, atomic_t *v)
{
	return GEN_BINARY_RMWcc(LOCK_PREFIX "subl", v->counter, e, "er", i);
}

static __always_inline void atomic_inc(atomic_t *v)
{
	asm volatile(LOCK_PREFIX "incl %0"
		     : "+m" (v->counter) :: "memory");
}

static __always_inline void atomic_dec(atomic_t *v)
{
	asm volatile(LOCK_PREFIX "decl %0"
		     : "+m" (v->counter) :: "memory");
}

static __always_inline bool atomic_dec_and_test(atomic_t *v)
{
	return GEN_UNARY_RMWcc(LOCK_PREFIX "decl", v->counter, e);
}

static __always_inline int atomic_fetch_add(int i, atomic_t *v)
{
	return xadd(&v->counter, i);
}
#define atomic_fetch_add atomic_fetch_add

static __always_inline int atomic_fetch_sub(int i, atomic_t *v)
{
	return xadd(&v->counter, -i);
}
#define atomic_fetch_sub atomic_fetch_sub

static __always_inline bool atomic_try_cmpxchg(atomic_t *v, int *old, int new)
{
	return try_cmpxchg(&v->counter, old, new);
}
#define atomic_try_cmpxchg atomic_try_cmpxchg

/* compiler.h, types.h already included above */

typedef struct {
	s64 __aligned(8) counter;
} atomic64_t;

#define ATOMIC64_INIT(val)	{ (val) }

#define __ATOMIC64_DECL(sym) void atomic64_##sym(atomic64_t *, ...)
#ifndef ATOMIC64_EXPORT
#define ATOMIC64_DECL_ONE __ATOMIC64_DECL
#else
#define ATOMIC64_DECL_ONE(sym) __ATOMIC64_DECL(sym); \
	ATOMIC64_EXPORT(atomic64_##sym)
#endif

/* CONFIG_X86_CMPXCHG64 not set - using _386 versions */
#define __alternative_atomic64(f, g, out, in...) \
	asm volatile("call %P[func]" \
		     : out : [func] "i" (atomic64_##f##_386), ## in)
#define ATOMIC64_DECL(sym) ATOMIC64_DECL_ONE(sym##_386)

#define alternative_atomic64(f, out, in...) \
	__alternative_atomic64(f, f, ASM_OUTPUT2(out), ## in)

/* Only declare assembly functions that are actually used */
ATOMIC64_DECL(read);
ATOMIC64_DECL(set);
ATOMIC64_DECL(inc_return);

#undef ATOMIC64_DECL
#undef ATOMIC64_DECL_ONE
#undef __ATOMIC64_DECL
#undef ATOMIC64_EXPORT

static inline void atomic64_set(atomic64_t *v, s64 i)
{
	unsigned high = (unsigned)(i >> 32);
	unsigned low = (unsigned)i;
	alternative_atomic64(set,  ,
			     "S" (v), "b" (low), "c" (high)
			     : "eax", "edx", "memory");
}

static inline s64 atomic64_read(const atomic64_t *v)
{
	s64 r;
	alternative_atomic64(read, "=&A" (r), "c" (v) : "memory");
	return r;
}

static inline s64 atomic64_inc_return(atomic64_t *v)
{
	s64 a;
	alternative_atomic64(inc_return, "=&A" (a),
			     "S" (v) : "memory", "ecx");
	return a;
}

#undef alternative_atomic64
#undef __alternative_atomic64

#endif
