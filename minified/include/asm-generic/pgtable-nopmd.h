/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PGTABLE_NOPMD_H
#define _PGTABLE_NOPMD_H

#include <asm-generic/pgtable-nopud.h>

#define __PAGETABLE_PMD_FOLDED 1

typedef struct {
	pud_t pud;
} pmd_t;

#define PMD_SHIFT PUD_SHIFT
#define PTRS_PER_PMD 1
#define PMD_SIZE (1UL << PMD_SHIFT)
#define PMD_MASK (~(PMD_SIZE - 1))

static inline pmd_t *pmd_offset(pud_t *pud, unsigned long address)
{
	return (pmd_t *)pud;
}

#define pmd_val(x) (pud_val((x).pud))
#define __pmd(x) ((pmd_t){ __pud(x) })

#endif /* _PGTABLE_NOPMD_H */
