#ifndef _LINUX_SWAPOPS_H
#define _LINUX_SWAPOPS_H
#include <linux/radix-tree.h>
#include <linux/bug.h>
#include <linux/mm_types.h>
#define SWP_TYPE_SHIFT	(BITS_PER_XA_VALUE - MAX_SWAPFILES_SHIFT)
#define SWP_OFFSET_MASK	((1UL << SWP_TYPE_SHIFT) - 1)
static inline pte_t pte_swp_clear_flags(pte_t pte) {
	if (pte_swp_exclusive(pte)) pte = pte_swp_clear_exclusive(pte);
	return pte;
}
static inline swp_entry_t swp_entry(unsigned long type, pgoff_t offset) { swp_entry_t ret; ret.val = (type << SWP_TYPE_SHIFT) | (offset & SWP_OFFSET_MASK); return ret; }
static inline pgoff_t swp_offset(swp_entry_t entry) { return entry.val & SWP_OFFSET_MASK; }
static inline int is_swap_pte(pte_t pte) { return !pte_none(pte) && !pte_present(pte); }
static inline swp_entry_t pte_to_swp_entry(pte_t pte) {
	swp_entry_t arch_entry;
	pte = pte_swp_clear_flags(pte);
	arch_entry = __pte_to_swp_entry(pte);
	return swp_entry(__swp_type(arch_entry), __swp_offset(arch_entry));
}
/* Unused stubs removed: is_device_private_entry, is_device_exclusive_entry,
   is_migration_entry, migration_entry_wait, pmd_migration_entry_wait,
   pmd_to_swp_entry, is_pmd_migration_entry, pte_marker typedef,
   PTE_MARKER_UFFD_WP, PTE_MARKER_MASK */
#endif
