#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/init.h>
#include <linux/list.h>
#include <asm/page.h>
#include <linux/rbtree.h>
#include <linux/overflow.h>

#include <asm/cpufeature.h>
#include <asm/pgtable_areas.h>

struct vm_area_struct;		 
struct notifier_block;		 

#define VM_ALLOC		0x00000002
#define VM_UNINITIALIZED	0x00000020
#define VM_NO_GUARD		0x00000040

#ifndef IOREMAP_MAX_ORDER
#define IOREMAP_MAX_ORDER	(7 + PAGE_SHIFT)	 
#endif

struct vm_struct {
	struct vm_struct	*next;
	void			*addr;
	unsigned long		size;
	unsigned long		flags;
	struct page		**pages;
	unsigned int		nr_pages;
	phys_addr_t		phys_addr;
	const void		*caller;
};

struct vmap_area {
	unsigned long va_start;
	unsigned long va_end;

	struct rb_node rb_node;          
	struct list_head list;           

	union {
		unsigned long subtree_max_size;  
		struct vm_struct *vm;            
	};
};

extern void __init vmalloc_init(void);

void *__vmalloc_node(unsigned long size, unsigned long align, gfp_t gfp_mask,
		int node, const void *caller) __alloc_size(1);

#ifndef ARCH_PAGE_TABLE_SYNC_MASK
#define ARCH_PAGE_TABLE_SYNC_MASK 0
#endif

void arch_sync_kernel_mappings(unsigned long start, unsigned long end);

#endif  
