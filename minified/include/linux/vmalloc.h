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

struct vm_struct;

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

#ifndef ARCH_PAGE_TABLE_SYNC_MASK
#define ARCH_PAGE_TABLE_SYNC_MASK 0
#endif

#endif  
