
#ifndef _ASM_X86_PGTABLE_32_TYPES_H
#define _ASM_X86_PGTABLE_32_TYPES_H

/* --- 2025-12-07 20:20 --- Inlined pgtable-2level_types.h */
#ifndef __ASSEMBLY__
#include <linux/types.h>

typedef unsigned long	pteval_t;
typedef unsigned long	pmdval_t;
typedef unsigned long	pudval_t;
typedef unsigned long	p4dval_t;
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;

typedef union {
	pteval_t pte;
	pteval_t pte_low;
} pte_t;
#endif

#define SHARED_KERNEL_PMD	0
#define ARCH_PAGE_TABLE_SYNC_MASK	PGTBL_PMD_MODIFIED
#define PGDIR_SHIFT	22
#define PTRS_PER_PGD	1024
#define PTRS_PER_PTE	1024
#define PGD_KERNEL_START	(CONFIG_PAGE_OFFSET >> PGDIR_SHIFT)

#define pgtable_l5_enabled() 0

#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE - 1))

#endif  
