#ifndef _LINUX_SWAPOPS_H
#define _LINUX_SWAPOPS_H
#include <linux/radix-tree.h>
#include <linux/bug.h>
#include <linux/mm_types.h>
/* SWP_TYPE_SHIFT, swp_entry, swp_offset removed - never called */
static inline int is_swap_pte(pte_t pte) { return !pte_none(pte) && !pte_present(pte); }
/* pte_swp_clear_flags, pte_to_swp_entry removed - never called */
#endif
