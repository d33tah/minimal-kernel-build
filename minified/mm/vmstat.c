/* VM statistics - minimal */
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/migrate.h>
atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS] __cacheline_aligned_in_smp;
#if defined(CONFIG_PROC_FS) || defined(CONFIG_SYSFS) || \
	defined(CONFIG_NUMA) || defined(CONFIG_MEMCG)
const char *const vmstat_text[] = {
	"nr_free_pages",
};
#endif
/* mm_percpu_wq workqueue removed - was allocated but never used */
void __init init_mm_internals(void)
{
}
