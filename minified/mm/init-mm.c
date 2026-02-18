/* MM initialization globals - consolidated from init-mm.c, vmstat.c, workingset.c */
#include <linux/mm.h>
#include <linux/list_lru.h>

#include <linux/user_namespace.h>

#ifndef INIT_MM_CONTEXT
#define INIT_MM_CONTEXT(name)
#endif

struct mm_struct init_mm = { .mm_rb = RB_ROOT,
			     .pgd = swapper_pg_dir,
			     .mm_users = ATOMIC_INIT(2),
			     .mm_count = ATOMIC_INIT(1),
			     MMAP_LOCK_INITIALIZER(init_mm).page_table_lock =
				     __SPIN_LOCK_UNLOCKED(
					     init_mm.page_table_lock),
			     .mmlist = LIST_HEAD_INIT(init_mm.mmlist),
			     .user_ns = &init_user_ns,
			     .cpu_bitmap = CPU_BITS_NONE,
			     INIT_MM_CONTEXT(init_mm) };

/* From vmstat.c */
atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS] __cacheline_aligned_in_smp;

/* From workingset.c */
struct list_lru shadow_nodes;
