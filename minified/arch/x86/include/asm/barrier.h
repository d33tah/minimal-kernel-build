/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BARRIER_H
#define _ASM_X86_BARRIER_H

/**
 * array_index_mask_nospec() - generate a mask that is ~0UL when the
 * 	bounds check succeeds and 0 otherwise
 * @index: array element index
 * @size: number of elements in array
 *
 * Returns:
 *     0 - (index < size)
 */
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

#endif /* _ASM_X86_BARRIER_H */
