
#ifndef _ASM_X86_BARRIER_H
#define _ASM_X86_BARRIER_H

#include <asm/alternative.h>
#include <asm/asm.h>
#include <linux/compiler.h>

#define mb() asm volatile(ALTERNATIVE("lock; addl $0,-4(%%esp)", "mfence", \
				      X86_FEATURE_XMM2) ::: "memory", "cc")
#define rmb() asm volatile(ALTERNATIVE("lock; addl $0,-4(%%esp)", "lfence", \
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

#ifndef nop
#define nop()	asm volatile ("nop")
#endif

#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()

#define smp_store_mb(var, value)  do { WRITE_ONCE(var, value); barrier(); } while (0)

#define smp_mb__after_atomic()	barrier()

#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	WRITE_ONCE(*p, v);						\
} while (0)

#define smp_load_acquire(p)						\
({									\
	__unqual_scalar_typeof(*p) ___p1 = READ_ONCE(*p);		\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	(typeof(*p))___p1;						\
})

#define smp_acquire__after_ctrl_dep()		smp_rmb()

#endif
