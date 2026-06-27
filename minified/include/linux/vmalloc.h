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
struct notifier_block;		 

#define VM_IOREMAP		0x00000001
#define VM_ALLOC		0x00000002
#define VM_UNINITIALIZED	0x00000020
#define VM_NO_GUARD		0x00000040
#define VM_ALLOW_HUGE_VMAP	0x00000400




struct vm_struct {
	struct vm_struct	*next;
	void			*addr;
	unsigned long		size;
	unsigned long		flags;
	struct page		**pages;
	unsigned int		nr_pages;
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

extern void *__vmalloc(unsigned long size, gfp_t gfp_mask) __alloc_size(1);
extern void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller) __alloc_size(1);
void *__vmalloc_node(unsigned long size, unsigned long align, gfp_t gfp_mask,
		int node, const void *caller) __alloc_size(1);

extern void vfree(const void *addr);



void arch_sync_kernel_mappings(unsigned long start, unsigned long end);


static inline size_t get_vm_area_size(const struct vm_struct *area)
{
	if (!(area->flags & VM_NO_GUARD))
		 
		return area->size - PAGE_SIZE;
	else
		return area->size;

}

extern struct vm_struct *remove_vm_area(const void *addr);





#endif  
