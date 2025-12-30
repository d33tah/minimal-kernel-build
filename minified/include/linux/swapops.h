#ifndef _LINUX_SWAPOPS_H
#define _LINUX_SWAPOPS_H
#include <linux/radix-tree.h>
#include <linux/bug.h>
#include <linux/mm_types.h>
#define SWP_TYPE_SHIFT	(BITS_PER_XA_VALUE - MAX_SWAPFILES_SHIFT)
#define SWP_OFFSET_MASK	((1UL << SWP_TYPE_SHIFT) - 1)
static inline swp_entry_t swp_entry(unsigned long type, pgoff_t offset) { swp_entry_t ret; ret.val = (type << SWP_TYPE_SHIFT) | (offset & SWP_OFFSET_MASK); return ret; }
static inline pgoff_t swp_offset(swp_entry_t entry) { return entry.val & SWP_OFFSET_MASK; }
static inline int is_swap_pte(pte_t pte) { return !pte_none(pte) && !pte_present(pte); }
/* pte_swp_clear_flags, pte_to_swp_entry removed - never called */
#endif
