 
 
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/vmstat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/math64.h>
#include <linux/writeback.h>
#include <linux/compaction.h>
#include <linux/mm_inline.h>
#include <linux/page_ext.h>
#include <linux/page_owner.h>
#include <linux/migrate.h>

#include "internal.h"



 
atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_numa_event[NR_VM_NUMA_EVENT_ITEMS] __cacheline_aligned_in_smp;





/* Minimal vmstat_text - only essential entries for basic kernel operation */
#if defined(CONFIG_PROC_FS) || defined(CONFIG_SYSFS) || \
    defined(CONFIG_NUMA) || defined(CONFIG_MEMCG)
const char * const vmstat_text[] = {
	"nr_free_pages",
};
#endif



struct workqueue_struct *mm_percpu_wq;

void __init init_mm_internals(void)
{
	int ret __maybe_unused;

	mm_percpu_wq = alloc_workqueue("mm_percpu_wq", WQ_MEM_RECLAIM, 0);

	migrate_on_reclaim_init();
}

