#ifndef _LINUX_SWAPOPS_H
#define _LINUX_SWAPOPS_H

#include <linux/radix-tree.h>
#include <linux/bug.h>
#include <linux/mm_types.h>


#define SWP_TYPE_SHIFT	(BITS_PER_XA_VALUE - MAX_SWAPFILES_SHIFT)
#define SWP_OFFSET_MASK	((1UL << SWP_TYPE_SHIFT) - 1)

static inline pte_t pte_swp_clear_flags(pte_t pte)
{
	if (pte_swp_exclusive(pte))
		pte = pte_swp_clear_exclusive(pte);
	if (pte_swp_soft_dirty(pte))
		pte = pte_swp_clear_soft_dirty(pte);
	if (pte_swp_uffd_wp(pte))
		pte = pte_swp_clear_uffd_wp(pte);
	return pte;
}

static inline swp_entry_t swp_entry(unsigned long type, pgoff_t offset)
{
	swp_entry_t ret;

	ret.val = (type << SWP_TYPE_SHIFT) | (offset & SWP_OFFSET_MASK);
	return ret;
}

static inline unsigned swp_type(swp_entry_t entry)
{
	return (entry.val >> SWP_TYPE_SHIFT);
}

static inline pgoff_t swp_offset(swp_entry_t entry)
{
	return entry.val & SWP_OFFSET_MASK;
}

static inline int is_swap_pte(pte_t pte)
{
	return !pte_none(pte) && !pte_present(pte);
}

static inline swp_entry_t pte_to_swp_entry(pte_t pte)
{
	swp_entry_t arch_entry;

	pte = pte_swp_clear_flags(pte);
	arch_entry = __pte_to_swp_entry(pte);
	return swp_entry(__swp_type(arch_entry), __swp_offset(arch_entry));
}

static inline pte_t swp_entry_to_pte(swp_entry_t entry)
{
	swp_entry_t arch_entry;

	arch_entry = __swp_entry(swp_type(entry), swp_offset(entry));
	return __swp_entry_to_pte(arch_entry);
}

/* radix_to_swp_entry, swp_to_radix_entry removed - unused */


/* CONFIG_DEVICE_PRIVATE disabled - minimal stubs */
static inline bool is_device_private_entry(swp_entry_t entry) { return false; }
static inline bool is_device_exclusive_entry(swp_entry_t entry) { return false; }
static inline int is_migration_entry(swp_entry_t swp) { return 0; }
static inline void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
					 unsigned long address) { }


typedef unsigned long pte_marker;

#define  PTE_MARKER_UFFD_WP  BIT(0)
#define  PTE_MARKER_MASK     (PTE_MARKER_UFFD_WP)


/* make_pte_marker_entry, is_pte_marker_entry, pte_marker_get removed - unused */
/* pfn_swap_entry_to_page, is_pfn_swap_entry removed - unused */

/* set_pmd_migration_entry, remove_migration_pmd, swp_entry_to_pmd removed - unused */
static inline void pmd_migration_entry_wait(struct mm_struct *m, pmd_t *p) { }
static inline swp_entry_t pmd_to_swp_entry(pmd_t pmd) { return swp_entry(0, 0); }
static inline int is_pmd_migration_entry(pmd_t pmd) { return 0; }


static inline int non_swap_entry(swp_entry_t entry)
{
	return swp_type(entry) >= MAX_SWAPFILES;
}

#endif  
