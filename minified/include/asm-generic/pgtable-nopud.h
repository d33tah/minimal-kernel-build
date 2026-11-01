/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PGTABLE_NOPUD_H
#define _PGTABLE_NOPUD_H

#include <asm-generic/pgtable-nop4d.h>

#define __PAGETABLE_PUD_FOLDED 1

typedef struct {
	p4d_t p4d;
} pud_t;

#define PUD_SHIFT P4D_SHIFT

static inline pud_t *pud_offset(p4d_t *p4d, unsigned long address)
{
	return (pud_t *)p4d;
}

#define pud_val(x) (p4d_val((x).p4d))
#define __pud(x) ((pud_t){ __p4d(x) })

#endif /* _PGTABLE_NOPUD_H */
