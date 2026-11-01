/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PGTABLE_NOP4D_H
#define _PGTABLE_NOP4D_H

#define __PAGETABLE_P4D_FOLDED 1

typedef struct {
	pgd_t pgd;
} p4d_t;

#define P4D_SHIFT PGDIR_SHIFT

static inline int pgd_none(pgd_t pgd)
{
	return 0;
}
static inline int pgd_bad(pgd_t pgd)
{
	return 0;
}

static inline p4d_t *p4d_offset(pgd_t *pgd, unsigned long address)
{
	return (p4d_t *)pgd;
}

#define p4d_val(x) (pgd_val((x).pgd))
#define __p4d(x) ((p4d_t){ __pgd(x) })

#endif /* _PGTABLE_NOP4D_H */
