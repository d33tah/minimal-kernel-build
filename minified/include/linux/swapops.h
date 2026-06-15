#ifndef _LINUX_SWAPOPS_H
#define _LINUX_SWAPOPS_H

#include <linux/radix-tree.h>
#include <linux/bug.h>
#include <linux/mm_types.h>


#define SWP_TYPE_SHIFT	(BITS_PER_XA_VALUE - MAX_SWAPFILES_SHIFT)
#define SWP_OFFSET_MASK	((1UL << SWP_TYPE_SHIFT) - 1)


typedef unsigned long pte_marker;

#define  PTE_MARKER_UFFD_WP  BIT(0)
#define  PTE_MARKER_MASK     (PTE_MARKER_UFFD_WP)


#endif
