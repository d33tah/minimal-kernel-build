/* VM statistics - minimal */
#include <linux/mm.h>
#include <linux/vmstat.h>
/* linux/migrate.h removed - empty header */
atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS] __cacheline_aligned_in_smp;
#if defined(CONFIG_PROC_FS) || defined(CONFIG_SYSFS) || \
	defined(CONFIG_NUMA) || defined(CONFIG_MEMCG)
const char *const vmstat_text[] = {
	"nr_free_pages",
};
#endif
/* mm_percpu_wq workqueue, init_mm_internals removed - empty stubs */
