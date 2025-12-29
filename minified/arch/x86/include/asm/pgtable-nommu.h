/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_NOMMU_H
#define _ASM_X86_PGTABLE_NOMMU_H

/*
 * EXPERIMENTAL: x86 NOMMU pgtable stubs
 * Minimal stubs - most pgtable operations are no-ops for NOMMU
 */

#include <asm/pgtable_types.h>
#include <asm/page.h>
#include <asm/fixmap.h>

/*
 * Trivial page table functions - all stubs for NOMMU
 */
#define pgd_present(pgd)	(1)
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)		do { } while (0)
/*
 * For x86 NOMMU, paging is still enabled (x86 requires it).
 * We need real page table walking, so implement proper PMD/PTE functions.
 * Using 2-level page tables (non-PAE): PMD is folded into PGD.
 */
#define pmd_none(pmd)		(!(pmd_val(pmd) & _PAGE_PRESENT))
#define pmd_bad(pmd)		(0)
#define pmd_present(pmd)	(!pmd_none(pmd))

/* Page table index calculations - need real values for page table walking */
#define pgd_index(a)		(((a) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pmd_index(a)		(0UL)  /* PMD folded into PGD for 2-level */
#define pte_index(a)		(((a) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pmd_same(a, b)		(pmd_val(a) == pmd_val(b))
#define pte_same(a, b)		(pte_val(a) == pte_val(b))

#define pte_pfn(pte)		((pte_val(pte) & PTE_PFN_MASK) >> PAGE_SHIFT)
#define pmd_pfn(pmd)		((pmd_val(pmd) & PTE_PFN_MASK) >> PAGE_SHIFT)
/* Construct a PTE from a PFN and protection flags */
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

/* Basic type constructors - stubs for NOMMU */
#define __pgd(x)		((pgd_t) { (x) })
#define __p4d(x)		((p4d_t) { __pgd(x) })
#define __pud(x)		((pud_t) { __p4d(x) })
#define __pmd(x)		((pmd_t) { __pud(x) })
#define __pte(x)		((pte_t) { .pte = (x) })

#define pgd_val(x)		((x).pgd)
#define p4d_val(x)		(pgd_val((x).pgd))
#define pud_val(x)		(p4d_val((x).p4d))
#define pmd_val(x)		(pud_val((x).pud))
#define pte_val(x)		((x).pte)

#define pte_unmap(pte)		do { } while (0)
#define pte_clear(mm, addr, ptep) do { } while (0)

/* Page table - provide real values for boot code compatibility */
extern pgd_t initial_page_table[];
#define KERNEL_PGD_PTRS		(PTRS_PER_PGD - __PAGE_OFFSET / PGDIR_SIZE)
#define LOWMEM_PAGES		((((_ULL(2)<<31) - __PAGE_OFFSET) >> PAGE_SHIFT))
#define PAGE_TABLE_SIZE(pages)	((pages) / PTRS_PER_PGD)

#define set_pmd_safe(pmdp, pmd)	do { } while (0)
#define set_pud_safe(pudp, pud)	do { } while (0)
#define set_p4d_safe(p4dp, p4d)	do { } while (0)
#define set_pgd_safe(pgdp, pgd)	do { } while (0)

#define pmd_page(pmd)		((struct page *)0)
#define pmd_page_vaddr(pmd)	(0UL)

#define PAGE_NONE	__pgprot(0)
#define PAGE_SHARED	__pgprot(0)
#define PAGE_COPY	__pgprot(0)
#define PAGE_READONLY	__pgprot(0)
/* For NOMMU, we still need proper page protection flags since x86 paging is always on */
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_EXEC __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)

/* For NOMMU, swapper_pg_dir points to initial_page_table to keep page tables valid */
extern pgd_t initial_page_table[];
#define swapper_pg_dir initial_page_table

#define KERNEL_PGD_BOUNDARY	(0)

/*
 * ZERO_PAGE is a global shared page that is always zero
 */
extern void *empty_zero_page;
#define ZERO_PAGE(vaddr)	(virt_to_page(empty_zero_page))

/*
 * vmalloc/kmap ranges - not used in NOMMU
 */
#define VMALLOC_START	0UL
#define VMALLOC_END	0UL

/* Stub functions */
static inline void clone_pgd_range(pgd_t *dst, pgd_t *src, int count) { }
/* For x86 NOMMU with paging enabled, we need real set_pte/set_pmd */
static inline void set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;
}
static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}
static inline pte_t pte_mkwrite_novma(pte_t pte) { return pte; }
static inline int pte_write(pte_t pte) { return 1; }
static inline int pte_dirty(pte_t pte) { return 0; }
static inline int pte_none(pte_t pte) { return !pte_val(pte); }
static inline int pte_young(pte_t pte) { return 0; }
static inline int pte_present(pte_t pte) { return 0; }
static inline pte_t pte_mkdirty(pte_t pte) { return pte; }
static inline pte_t pte_mkyoung(pte_t pte) { return pte; }
static inline unsigned long pte_pfn_fn(pte_t pte) { return 0; }
static inline unsigned long pmd_pfn_fn(pmd_t pmd) { return 0; }

/* Page table walking - use generic versions */
#include <asm-generic/pgtable-nopmd.h>

/*
 * Extract PTE table address from PMD and add index.
 * For 2-level x86, PMD is folded into PGD, so this extracts from PGD entry.
 */
static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long addr)
{
	unsigned long pte_table_phys = pmd_val(*pmd) & PTE_PFN_MASK;
	return (pte_t *)__va(pte_table_phys) + pte_index(addr);
}
#define pte_offset_kernel pte_offset_kernel

/* Protection keys - stub for NOMMU */
static inline bool __pkru_allows_pkey(u16 pkey, bool write) { return true; }
static inline u16 pte_flags_pkey(unsigned long pte_flags) { return 0; }
static inline u32 read_pkru(void) { return 0; }
static inline void write_pkru(u32 pkru) { }
static inline void pkru_write_default(void) { }
static inline u32 pkru_get_init_value(void) { return 0; }

/* PTE flag manipulation stubs */
static inline pte_t pte_set_flags(pte_t pte, pteval_t set) { return pte; }
static inline pte_t pte_clear_flags(pte_t pte, pteval_t clear) { return pte; }

/* direct_gbpages defined in init.c, wrapped in CONFIG_MMU */

/* MMU-specific stubs */
static inline int pte_exec(pte_t pte) { return 1; }
static inline struct mm_struct *pgd_page_get_mm(struct page *page) { return NULL; }
static inline int __pte_needs_invert(u64 val) { return 0; }
static inline int is_new_memtype_allowed(u64 paddr, unsigned long size,
			enum page_cache_mode pcm, enum page_cache_mode new_pcm) { return 1; }

/* Memory conversion stubs */
#define pages_to_mb(x) ((x) >> (20 - PAGE_SHIFT))

/* Fixed address space - included from asm/fixmap.h above */

/* Arch context switch stubs */
#define arch_end_context_switch(prev)	do { } while (0)

/* mk_pte and set_pte_at stubs */
#define mk_pte(page, prot)	__pte(0)
#define set_pte_at(mm, addr, ptep, pte) do { } while (0)

/* Page table initialization - for x86 NOMMU, we still need page tables!
 * The x86 MMU is always enabled, so we need proper mappings.
 * early_alloc_pgt_buf and init_mem_mapping are NOT stubbed - they're real.
 * See arch/x86/mm/init.c for implementations.
 */
void init_mem_mapping(void);
void early_alloc_pgt_buf(void);
static inline void sync_initial_page_table(void) { }
/* paging_init is defined in init_32.c with a NOMMU stub */

/* These are provided by mm/pgtable-generic.c for NOMMU */

#endif /* _ASM_X86_PGTABLE_NOMMU_H */
