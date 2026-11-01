/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_32_H
#define _ASM_X86_PGTABLE_32_H

#include <asm/pgtable_32_types.h>

/*
 * The Linux memory management assumes a three-level page table setup. On
 * the i386, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * i386 mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the i386 page table tree.
 */
#ifndef __ASSEMBLY__

struct mm_struct;

extern pgd_t swapper_pg_dir[1024];
extern pgd_t initial_page_table[1024];

static inline void native_set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;
}
static inline void native_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}
static inline void native_pte_clear(struct mm_struct *mm, unsigned long addr,
				    pte_t *xp)
{
	*xp = native_make_pte(0);
}
static inline u64 protnone_mask(u64 val)
{
	return 0;
}

#endif

#define PAGE_TABLE_SIZE(pages) ((pages) / PTRS_PER_PGD)

/*
 * MKB_FOOTPRINT: map at most 64 MiB of RAM (upstream: the whole 1 GiB above
 * PAGE_OFFSET); sizes the brk page tables (1 MiB -> 64 KiB) and MAXMEM.
 */
#define LOWMEM_PAGES ((_ULL(64) << 20) >> PAGE_SHIFT)

#endif
