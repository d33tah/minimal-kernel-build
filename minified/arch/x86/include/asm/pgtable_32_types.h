#ifndef _ASM_X86_PGTABLE_32_TYPES_H
#define _ASM_X86_PGTABLE_32_TYPES_H

#ifndef __ASSEMBLY__

typedef unsigned long pteval_t;
typedef unsigned long pmdval_t;
typedef unsigned long pgdval_t;
typedef unsigned long pgprotval_t;

typedef union {
	pteval_t pte;
} pte_t;
#endif

#define PGDIR_SHIFT 22
#define PTRS_PER_PGD 1024
#define PTRS_PER_PTE 1024

#endif
