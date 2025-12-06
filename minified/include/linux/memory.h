#ifndef _LINUX_MEMORY_H_
#define _LINUX_MEMORY_H_

#include <linux/node.h>
#include <linux/compiler.h>
#include <linux/mutex.h>
#include <linux/notifier.h>

#define MIN_MEMORY_BLOCK_SIZE     (1UL << SECTION_SIZE_BITS)

struct memory_group {
	int nid;
	struct list_head memory_blocks;
	unsigned long present_kernel_pages;
	unsigned long present_movable_pages;
	bool is_dynamic;
	union {
		struct {
			unsigned long max_pages;
		} s;
		struct {
			unsigned long unit_pages;
		} d;
	};
};

struct memory_block {
	unsigned long start_section_nr;
	unsigned long state;		 
	int online_type;		 
	int nid;			 
	 
	struct zone *zone;
	struct device dev;
	 
	unsigned long nr_vmemmap_pages;
	struct memory_group *group;	 
	struct list_head group_next;	 
};

int arch_get_memory_phys_device(unsigned long start_pfn);
unsigned long memory_block_size_bytes(void);
int set_memory_block_size_order(unsigned int order);

#define	MEM_ONLINE		(1<<0)  
#define	MEM_GOING_OFFLINE	(1<<1)  
#define	MEM_OFFLINE		(1<<2)  
#define	MEM_GOING_ONLINE	(1<<3)
#define	MEM_CANCEL_ONLINE	(1<<4)
#define	MEM_CANCEL_OFFLINE	(1<<5)

struct memory_notify {
	unsigned long start_pfn;
	unsigned long nr_pages;
	int status_change_nid_normal;
	int status_change_nid;
};

struct notifier_block;
struct mem_section;

#define SLAB_CALLBACK_PRI       1
#define IPC_CALLBACK_PRI        10

static inline void memory_dev_init(void)
{
	return;
}
static inline int register_memory_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline void unregister_memory_notifier(struct notifier_block *nb)
{
}
static inline int memory_notify(unsigned long val, void *v)
{
	return 0;
}
static inline int hotplug_memory_notifier(notifier_fn_t fn, int pri)
{
	return 0;
}
#define register_hotmemory_notifier(nb)    ({ (void)(nb); 0; })
#define unregister_hotmemory_notifier(nb)  ({ (void)(nb); })

extern struct mutex text_mutex;

#endif  
