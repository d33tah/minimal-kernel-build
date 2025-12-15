 
#ifndef _ASM_X86_BARRIER_H
#define _ASM_X86_BARRIER_H

#include <asm/alternative.h>
#include <asm/nops.h>

 

#define mb() asm volatile(ALTERNATIVE("lock; addl $0,-4(%%esp)", "mfence", \
				      X86_FEATURE_XMM2) ::: "memory", "cc")
#define rmb() asm volatile(ALTERNATIVE("lock; addl $0,-4(%%esp)", "lfence", \
				       X86_FEATURE_XMM2) ::: "memory", "cc")
#define wmb() asm volatile(ALTERNATIVE("lock; addl $0,-4(%%esp)", "sfence", \
				       X86_FEATURE_XMM2) ::: "memory", "cc")

 
static inline unsigned long array_index_mask_nospec(unsigned long index,
		unsigned long size)
{
	unsigned long mask;

	asm volatile ("cmp %1,%2; sbb %0,%0;"
			:"=r" (mask)
			:"g"(size),"r" (index)
			:"cc");
	return mask;
}

 
#define array_index_mask_nospec array_index_mask_nospec

 
#define barrier_nospec() alternative("", "lfence", X86_FEATURE_LFENCE_RDTSC)

#define __dma_rmb()	barrier()
#define __dma_wmb()	barrier()

#define __smp_mb()	asm volatile("lock; addl $0,-4(%%" _ASM_SP ")" ::: "memory", "cc")

#define __smp_rmb()	dma_rmb()
#define __smp_wmb()	barrier()
#define __smp_store_mb(var, value) do { (void)xchg(&var, value); } while (0)

#define __smp_store_release(p, v)					\
do {									\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	WRITE_ONCE(*p, v);						\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	___p1;								\
})

 
#define __smp_mb__before_atomic()	do { } while (0)
#define __smp_mb__after_atomic()	do { } while (0)

/* --- 2025-12-07 10:25 --- Inlined asm-generic/barrier.h content */
#include <linux/compiler.h>
#include <asm/rwonce.h>

#ifndef nop
#define nop()	asm volatile ("nop")
#endif

#ifdef __mb
#define mb()	do {  __mb(); } while (0)
#endif

#ifdef __rmb
#define rmb()	do {  __rmb(); } while (0)
#endif

#ifdef __wmb
#define wmb()	do {  __wmb(); } while (0)
#endif

#ifdef __dma_rmb
#define dma_rmb()	do {  __dma_rmb(); } while (0)
#endif

#ifdef __dma_wmb
#define dma_wmb()	do {  __dma_wmb(); } while (0)
#endif

#ifndef mb
#define mb()	barrier()
#endif

#ifndef rmb
#define rmb()	mb()
#endif

#ifndef wmb
#define wmb()	mb()
#endif

#ifndef dma_rmb
#define dma_rmb()	rmb()
#endif

#ifndef dma_wmb
#define dma_wmb()	wmb()
#endif

#ifndef __smp_mb
#define __smp_mb()	mb()
#endif

#ifndef __smp_rmb
#define __smp_rmb()	rmb()
#endif

#ifndef __smp_wmb
#define __smp_wmb()	wmb()
#endif

#ifndef smp_mb
#define smp_mb()	barrier()
#endif

#ifndef smp_rmb
#define smp_rmb()	barrier()
#endif

#ifndef smp_wmb
#define smp_wmb()	barrier()
#endif

#ifndef __smp_store_mb
#define __smp_store_mb(var, value)  do { WRITE_ONCE(var, value); __smp_mb(); } while (0)
#endif

#ifndef __smp_mb__before_atomic
#define __smp_mb__before_atomic()	__smp_mb()
#endif

#ifndef __smp_mb__after_atomic
#define __smp_mb__after_atomic()	__smp_mb()
#endif

#ifndef __smp_store_release
#define __smp_store_release(p, v)					\
do {									\
	compiletime_assert_atomic_type(*p);				\
	__smp_mb();							\
	WRITE_ONCE(*p, v);						\
} while (0)
#endif

#ifndef __smp_load_acquire
#define __smp_load_acquire(p)						\
({									\
	__unqual_scalar_typeof(*p) ___p1 = READ_ONCE(*p);		\
	compiletime_assert_atomic_type(*p);				\
	__smp_mb();							\
	(typeof(*p))___p1;						\
})
#endif

#ifndef smp_store_mb
#define smp_store_mb(var, value)  do { WRITE_ONCE(var, value); barrier(); } while (0)
#endif

#ifndef smp_mb__before_atomic
#define smp_mb__before_atomic()	barrier()
#endif

#ifndef smp_mb__after_atomic
#define smp_mb__after_atomic()	barrier()
#endif

#ifndef smp_store_release
#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	WRITE_ONCE(*p, v);						\
} while (0)
#endif

#ifndef smp_load_acquire
#define smp_load_acquire(p)						\
({									\
	__unqual_scalar_typeof(*p) ___p1 = READ_ONCE(*p);		\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	(typeof(*p))___p1;						\
})
#endif

#define virt_rmb() do {  __smp_rmb(); } while (0)

#ifndef smp_acquire__after_ctrl_dep
#define smp_acquire__after_ctrl_dep()		smp_rmb()
#endif

#ifndef smp_cond_load_relaxed
#define smp_cond_load_relaxed(ptr, cond_expr) ({		\
	typeof(ptr) __PTR = (ptr);				\
	__unqual_scalar_typeof(*ptr) VAL;			\
	for (;;) {						\
		VAL = READ_ONCE(*__PTR);			\
		if (cond_expr)					\
			break;					\
		cpu_relax();					\
	}							\
	(typeof(*ptr))VAL;					\
})
#endif

#ifndef smp_cond_load_acquire
#define smp_cond_load_acquire(ptr, cond_expr) ({		\
	__unqual_scalar_typeof(*ptr) _val;			\
	_val = smp_cond_load_relaxed(ptr, cond_expr);		\
	smp_acquire__after_ctrl_dep();				\
	(typeof(*ptr))_val;					\
})
#endif
/* end asm-generic/barrier.h */

#endif  
