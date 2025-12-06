/* VM statistics - minimal */
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/workqueue.h>
#include <linux/migrate.h>



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

