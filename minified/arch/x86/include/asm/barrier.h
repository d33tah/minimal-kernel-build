
#ifndef _ASM_X86_BARRIER_H
#define _ASM_X86_BARRIER_H

static inline unsigned long array_index_mask_nospec(unsigned long index,
						    unsigned long size)
{
	unsigned long mask;

	asm volatile("cmp %1,%2; sbb %0,%0;"
		     : "=r"(mask)
		     : "g"(size), "r"(index)
		     : "cc");
	return mask;
}

/* --- 2025-12-07 10:25 --- Inlined asm-generic/barrier.h content */

/* nop() fallback removed - nop() never invoked anywhere (statically dead) */

/* #ifdef __mb/__rmb/__wmb fallback blocks removed - __mb/__rmb/__wmb never #defined (statically dead) */

#define smp_rmb() barrier()

#define smp_wmb() barrier()

#define smp_store_release(p, v)                     \
	do {                                        \
		compiletime_assert_atomic_type(*p); \
		barrier();                          \
		WRITE_ONCE(*p, v);                  \
	} while (0)

#define smp_load_acquire(p)                                       \
	({                                                        \
		__unqual_scalar_typeof(*p) ___p1 = READ_ONCE(*p); \
		compiletime_assert_atomic_type(*p);               \
		barrier();                                        \
		(typeof(*p))___p1;                                \
	})

#define smp_acquire__after_ctrl_dep() smp_rmb()

/* end asm-generic/barrier.h */

#endif
