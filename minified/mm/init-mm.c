/* MM initialization globals - consolidated from init-mm.c, vmstat.c, workingset.c */
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/list_lru.h>
#include <linux/mm_types.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/mman.h>
#include <linux/pgtable.h>

#include <linux/atomic.h>
#include <linux/user_namespace.h>

#include <asm/mmu.h>

#ifndef INIT_MM_CONTEXT
#define INIT_MM_CONTEXT(name)
#endif

struct mm_struct init_mm = { .mm_rb = RB_ROOT,
			     .pgd = swapper_pg_dir,
			     .mm_users = ATOMIC_INIT(2),
			     .mm_count = ATOMIC_INIT(1),
			     /* write_protect_seq removed - never used */
			     MMAP_LOCK_INITIALIZER(init_mm).page_table_lock =
				     __SPIN_LOCK_UNLOCKED(
					     init_mm.page_table_lock),
			     /* .arg_lock init removed - field was removed */
			     .mmlist = LIST_HEAD_INIT(init_mm.mmlist),
			     .user_ns = &init_user_ns,
			     .cpu_bitmap = CPU_BITS_NONE,
			     INIT_MM_CONTEXT(init_mm) };

void setup_initial_init_mm(void *start_code, void *end_code, void *end_data,
			   void *brk)
{
	/* start_code/end_code/end_data/brk assignments removed - write-only fields */
}

/* From vmstat.c */
atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS] __cacheline_aligned_in_smp;

/* From workingset.c */
struct list_lru shadow_nodes;
