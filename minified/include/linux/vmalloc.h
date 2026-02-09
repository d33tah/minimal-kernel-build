#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

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
/* VM_MAP_PUT_PAGES removed - never used */
#define VM_ALLOW_HUGE_VMAP	0x00000400

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

/* arch_vmap_pmd_supported, arch_vmap_pte_supported_shift removed - no callers */

extern void __init vmalloc_init(void);

/* vmalloc, __vmalloc removed - never called */
extern void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller) __alloc_size(1);
void *__vmalloc_node(unsigned long size, unsigned long align, gfp_t gfp_mask,
		int node, const void *caller) __alloc_size(1);

/* vfree, vunmap removed - callers stubbed out */
/* vmap removed - never called */

#ifndef ARCH_PAGE_TABLE_SYNC_MASK
#define ARCH_PAGE_TABLE_SYNC_MASK 0
#endif

void arch_sync_kernel_mappings(unsigned long start, unsigned long end);

/* get_vm_area_size inlined at mm/vmalloc.c - single caller */

void free_vm_area(struct vm_struct *area);
extern struct vm_struct *remove_vm_area(const void *addr);

#endif  
