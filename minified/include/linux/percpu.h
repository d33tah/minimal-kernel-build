 
#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/mmdebug.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/pfn.h>
#include <linux/init.h>

#include <asm/percpu.h>

 
#define PERCPU_MODULE_RESERVE		0

 
#define PCPU_MIN_UNIT_SIZE		PFN_ALIGN(32 << 10)

 
#define PCPU_MIN_ALLOC_SHIFT		2
#define PCPU_MIN_ALLOC_SIZE		(1 << PCPU_MIN_ALLOC_SHIFT)

 
#define PCPU_BITMAP_BLOCK_SIZE		PAGE_SIZE
#define PCPU_BITMAP_BLOCK_BITS		(PCPU_BITMAP_BLOCK_SIZE >>	\
					 PCPU_MIN_ALLOC_SHIFT)

 
#define PERCPU_DYNAMIC_EARLY_SLOTS	128
#define PERCPU_DYNAMIC_EARLY_SIZE	(12 << 10)

 
#if BITS_PER_LONG > 32
#define PERCPU_DYNAMIC_RESERVE		(28 << 10)
#else
#define PERCPU_DYNAMIC_RESERVE		(20 << 10)
#endif

extern void *pcpu_base_addr;
extern const unsigned long *pcpu_unit_offsets;

struct pcpu_group_info {
	int			nr_units;	 
	unsigned long		base_offset;	 
	unsigned int		*cpu_map;	 
};

struct pcpu_alloc_info {
	size_t			static_size;
	size_t			reserved_size;
	size_t			dyn_size;
	size_t			unit_size;
	size_t			atom_size;
	size_t			alloc_size;
	size_t			__ai_size;	 
	int			nr_groups;	 
	struct pcpu_group_info	groups[];
};

enum pcpu_fc {
	PCPU_FC_AUTO,
	PCPU_FC_EMBED,
	PCPU_FC_PAGE,

	PCPU_FC_NR,
};
extern const char * const pcpu_fc_names[PCPU_FC_NR];

extern enum pcpu_fc pcpu_chosen_fc;

typedef int (pcpu_fc_cpu_to_node_fn_t)(int cpu);
typedef int (pcpu_fc_cpu_distance_fn_t)(unsigned int from, unsigned int to);

extern struct pcpu_alloc_info * __init pcpu_alloc_alloc_info(int nr_groups,
							     int nr_units);
extern void __init pcpu_free_alloc_info(struct pcpu_alloc_info *ai);

extern void __init pcpu_setup_first_chunk(const struct pcpu_alloc_info *ai,
					 void *base_addr);

extern int __init pcpu_embed_first_chunk(size_t reserved_size, size_t dyn_size,
				size_t atom_size,
				pcpu_fc_cpu_distance_fn_t cpu_distance_fn,
				pcpu_fc_cpu_to_node_fn_t cpu_to_nd_fn);

void __init pcpu_populate_pte(unsigned long addr);
extern int __init pcpu_page_first_chunk(size_t reserved_size,
				pcpu_fc_cpu_to_node_fn_t cpu_to_nd_fn);

extern void __percpu *__alloc_reserved_percpu(size_t size, size_t align) __alloc_size(1);
extern bool __is_kernel_percpu_address(unsigned long addr, unsigned long *can_addr);
extern bool is_kernel_percpu_address(unsigned long addr);

extern void __init setup_per_cpu_areas(void);

extern void __percpu *__alloc_percpu_gfp(size_t size, size_t align, gfp_t gfp) __alloc_size(1);
extern void __percpu *__alloc_percpu(size_t size, size_t align) __alloc_size(1);
extern void free_percpu(void __percpu *__pdata);
extern phys_addr_t per_cpu_ptr_to_phys(void *addr);

#define alloc_percpu_gfp(type, gfp)					\
	(typeof(type) __percpu *)__alloc_percpu_gfp(sizeof(type),	\
						__alignof__(type), gfp)
#define alloc_percpu(type)						\
	(typeof(type) __percpu *)__alloc_percpu(sizeof(type),		\
						__alignof__(type))

extern unsigned long pcpu_nr_pages(void);

#endif  
