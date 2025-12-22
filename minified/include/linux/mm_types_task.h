#ifndef _LINUX_MM_TYPES_TASK_H
#define _LINUX_MM_TYPES_TASK_H


#include <linux/types.h>
#include <linux/threads.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>

#include <asm/page.h>

/* Inlined from asm/tlbbatch.h */
struct arch_tlbflush_unmap_batch {
	struct cpumask cpumask;
};

/* NR_CPUS=1 < CONFIG_SPLIT_PTLOCK_CPUS=4, so USE_SPLIT_*=0 */
#define USE_SPLIT_PTE_PTLOCKS	0
#define USE_SPLIT_PMD_PTLOCKS	0
#define ALLOC_SPLIT_PTLOCKS	0

#define VMACACHE_BITS 2
#define VMACACHE_SIZE (1U << VMACACHE_BITS)
#define VMACACHE_MASK (VMACACHE_SIZE - 1)

struct vmacache {
	u64 seqnum;
	struct vm_area_struct *vmas[VMACACHE_SIZE];
};

enum {
	MM_FILEPAGES,
	MM_ANONPAGES,
	MM_SWAPENTS,
	MM_SHMEMPAGES,
	NR_MM_COUNTERS
};

/* USE_SPLIT_PTE_PTLOCKS=0, no SPLIT_RSS_COUNTING */

struct mm_rss_stat {
	atomic_long_t count[NR_MM_COUNTERS];
};

/* 32-bit x86: BITS_PER_LONG=32, PAGE_SIZE=4096 */
struct page_frag {
	struct page *page;
	__u16 offset;
	__u16 size;
};

struct tlbflush_unmap_batch {
	 
	struct arch_tlbflush_unmap_batch arch;

	 
	bool flush_required;

	 
	bool writable;
};

#endif  
