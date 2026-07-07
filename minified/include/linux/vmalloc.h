#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <asm/page.h>
#include <linux/rbtree.h>
#include <linux/overflow.h>

/* Inlined from asm/vmalloc.h */
#include <asm/cpufeature.h>
#include <asm/pgtable_areas.h>

struct vm_area_struct;		 

/* VM_NO_GUARD removed - 0-caller vmalloc flag (not in any composite mask) */
#define VM_ALLOW_HUGE_VMAP	0x00000400




struct vmap_area {
	unsigned long va_start;
	unsigned long va_end;

	struct rb_node rb_node;
	struct list_head list;

	unsigned long subtree_max_size;
};



extern void __init vmalloc_init(void);

extern void *__vmalloc(unsigned long size, gfp_t gfp_mask) __alloc_size(1);
extern void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller) __alloc_size(1);
void *__vmalloc_node(unsigned long size, unsigned long align, gfp_t gfp_mask,
		int node, const void *caller) __alloc_size(1);

extern void vfree(const void *addr);


/* get_vm_area_size removed - 0-caller orphan */





#endif  
