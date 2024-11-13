// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/vmstat.c
 *
 *  Manages VM statistics
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  zoned VM statistics
 *  Copyright (C) 2006 Silicon Graphics, Inc.,
 *		Christoph Lameter <christoph@lameter.com>
 *  Copyright (C) 2008-2014 Christoph Lameter
 */
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

#ifdef CONFIG_NUMA
int sysctl_vm_numa_stat = ENABLE_NUMA_STAT;

/* zero numa counters within a zone */
static void zero_zone_numa_counters(struct zone *zone)
{
	int item, cpu;

	for (item = 0; item < NR_VM_NUMA_EVENT_ITEMS; item++) {
		atomic_long_set(&zone->vm_numa_event[item], 0);
		for_each_online_cpu(cpu) {
			per_cpu_ptr(zone->per_cpu_zonestats, cpu)->vm_numa_event[item]
						= 0;
		}
	}
}

/* zero numa counters of all the populated zones */
static void zero_zones_numa_counters(void)
{
	struct zone *zone;

	for_each_populated_zone(zone)
		zero_zone_numa_counters(zone);
}

/* zero global numa counters */
static void zero_global_numa_counters(void)
{
	int item;

	for (item = 0; item < NR_VM_NUMA_EVENT_ITEMS; item++)
		atomic_long_set(&vm_numa_event[item], 0);
}

static void invalid_numa_statistics(void)
{
	zero_zones_numa_counters();
	zero_global_numa_counters();
}

static DEFINE_MUTEX(vm_numa_stat_lock);

int sysctl_vm_numa_stat_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int ret, oldval;

	mutex_lock(&vm_numa_stat_lock);
	if (write)
		oldval = sysctl_vm_numa_stat;
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (ret || !write)
		goto out;

	if (oldval == sysctl_vm_numa_stat)
		goto out;
	else if (sysctl_vm_numa_stat == ENABLE_NUMA_STAT) {
		static_branch_enable(&vm_numa_stat_key);
		pr_info("enable numa statistics\n");
	} else {
		static_branch_disable(&vm_numa_stat_key);
		invalid_numa_statistics();
		pr_info("disable numa statistics, and clear numa counters\n");
	}

out:
	mutex_unlock(&vm_numa_stat_lock);
	return ret;
}
#endif


/*
 * Manage combined zone based / global counters
 *
 * vm_stat contains the global counters
 */
atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_numa_event[NR_VM_NUMA_EVENT_ITEMS] __cacheline_aligned_in_smp;
EXPORT_SYMBOL(vm_zone_stat);
EXPORT_SYMBOL(vm_node_stat);

#ifdef CONFIG_NUMA
static void fold_vm_zone_numa_events(struct zone *zone)
{
	unsigned long zone_numa_events[NR_VM_NUMA_EVENT_ITEMS] = { 0, };
	int cpu;
	enum numa_stat_item item;

	for_each_online_cpu(cpu) {
		struct per_cpu_zonestat *pzstats;

		pzstats = per_cpu_ptr(zone->per_cpu_zonestats, cpu);
		for (item = 0; item < NR_VM_NUMA_EVENT_ITEMS; item++)
			zone_numa_events[item] += xchg(&pzstats->vm_numa_event[item], 0);
	}

	for (item = 0; item < NR_VM_NUMA_EVENT_ITEMS; item++)
		zone_numa_event_add(zone_numa_events[item], zone, item);
}

void fold_vm_numa_events(void)
{
	struct zone *zone;

	for_each_populated_zone(zone)
		fold_vm_zone_numa_events(zone);
}
#endif


#ifdef CONFIG_NUMA
/*
 * Determine the per node value of a stat item. This function
 * is called frequently in a NUMA machine, so try to be as
 * frugal as possible.
 */
unsigned long sum_zone_node_page_state(int node,
				 enum zone_stat_item item)
{
	struct zone *zones = NODE_DATA(node)->node_zones;
	int i;
	unsigned long count = 0;

	for (i = 0; i < MAX_NR_ZONES; i++)
		count += zone_page_state(zones + i, item);

	return count;
}

/* Determine the per node value of a numa stat item. */
unsigned long sum_zone_numa_event_state(int node,
				 enum numa_stat_item item)
{
	struct zone *zones = NODE_DATA(node)->node_zones;
	unsigned long count = 0;
	int i;

	for (i = 0; i < MAX_NR_ZONES; i++)
		count += zone_numa_event_state(zones + i, item);

	return count;
}

/*
 * Determine the per node value of a stat item.
 */
unsigned long node_page_state_pages(struct pglist_data *pgdat,
				    enum node_stat_item item)
{
	long x = atomic_long_read(&pgdat->vm_stat[item]);
	return x;
}

unsigned long node_page_state(struct pglist_data *pgdat,
			      enum node_stat_item item)
{
	VM_WARN_ON_ONCE(vmstat_item_in_bytes(item));

	return node_page_state_pages(pgdat, item);
}
#endif


#if defined(CONFIG_PROC_FS) || defined(CONFIG_SYSFS) || \
    defined(CONFIG_NUMA) || defined(CONFIG_MEMCG)
#define TEXT_FOR_DMA(xx)

#ifdef CONFIG_ZONE_DMA32
#define TEXT_FOR_DMA32(xx) xx "_dma32",
#else
#define TEXT_FOR_DMA32(xx)
#endif

#ifdef CONFIG_HIGHMEM
#define TEXT_FOR_HIGHMEM(xx) xx "_high",
#else
#define TEXT_FOR_HIGHMEM(xx)
#endif

#define TEXTS_FOR_ZONES(xx) TEXT_FOR_DMA(xx) TEXT_FOR_DMA32(xx) xx "_normal", \
					TEXT_FOR_HIGHMEM(xx) xx "_movable",

const char * const vmstat_text[] = {
	/* enum zone_stat_item counters */
	"nr_free_pages",
	"nr_zone_inactive_anon",
	"nr_zone_active_anon",
	"nr_zone_inactive_file",
	"nr_zone_active_file",
	"nr_zone_unevictable",
	"nr_zone_write_pending",
	"nr_mlock",
	"nr_bounce",
#if IS_ENABLED(CONFIG_ZSMALLOC)
	"nr_zspages",
#endif
	"nr_free_cma",

	/* enum numa_stat_item counters */
#ifdef CONFIG_NUMA
	"numa_hit",
	"numa_miss",
	"numa_foreign",
	"numa_interleave",
	"numa_local",
	"numa_other",
#endif

	/* enum node_stat_item counters */
	"nr_inactive_anon",
	"nr_active_anon",
	"nr_inactive_file",
	"nr_active_file",
	"nr_unevictable",
	"nr_slab_reclaimable",
	"nr_slab_unreclaimable",
	"nr_isolated_anon",
	"nr_isolated_file",
	"workingset_nodes",
	"workingset_refault_anon",
	"workingset_refault_file",
	"workingset_activate_anon",
	"workingset_activate_file",
	"workingset_restore_anon",
	"workingset_restore_file",
	"workingset_nodereclaim",
	"nr_anon_pages",
	"nr_mapped",
	"nr_file_pages",
	"nr_dirty",
	"nr_writeback",
	"nr_writeback_temp",
	"nr_shmem",
	"nr_shmem_hugepages",
	"nr_shmem_pmdmapped",
	"nr_file_hugepages",
	"nr_file_pmdmapped",
	"nr_anon_transparent_hugepages",
	"nr_vmscan_write",
	"nr_vmscan_immediate_reclaim",
	"nr_dirtied",
	"nr_written",
	"nr_throttled_written",
	"nr_kernel_misc_reclaimable",
	"nr_foll_pin_acquired",
	"nr_foll_pin_released",
	"nr_kernel_stack",
#if IS_ENABLED(CONFIG_SHADOW_CALL_STACK)
	"nr_shadow_call_stack",
#endif
	"nr_page_table_pages",
#ifdef CONFIG_SWAP
	"nr_swapcached",
#endif
#ifdef CONFIG_NUMA_BALANCING
	"pgpromote_success",
#endif

	/* enum writeback_stat_item counters */
	"nr_dirty_threshold",
	"nr_dirty_background_threshold",

#if defined(CONFIG_VM_EVENT_COUNTERS) || defined(CONFIG_MEMCG)
	/* enum vm_event_item counters */
	"pgpgin",
	"pgpgout",
	"pswpin",
	"pswpout",

	TEXTS_FOR_ZONES("pgalloc")
	TEXTS_FOR_ZONES("allocstall")
	TEXTS_FOR_ZONES("pgskip")

	"pgfree",
	"pgactivate",
	"pgdeactivate",
	"pglazyfree",

	"pgfault",
	"pgmajfault",
	"pglazyfreed",

	"pgrefill",
	"pgreuse",
	"pgsteal_kswapd",
	"pgsteal_direct",
	"pgdemote_kswapd",
	"pgdemote_direct",
	"pgscan_kswapd",
	"pgscan_direct",
	"pgscan_direct_throttle",
	"pgscan_anon",
	"pgscan_file",
	"pgsteal_anon",
	"pgsteal_file",

#ifdef CONFIG_NUMA
	"zone_reclaim_failed",
#endif
	"pginodesteal",
	"slabs_scanned",
	"kswapd_inodesteal",
	"kswapd_low_wmark_hit_quickly",
	"kswapd_high_wmark_hit_quickly",
	"pageoutrun",

	"pgrotated",

	"drop_pagecache",
	"drop_slab",
	"oom_kill",

#ifdef CONFIG_NUMA_BALANCING
	"numa_pte_updates",
	"numa_huge_pte_updates",
	"numa_hint_faults",
	"numa_hint_faults_local",
	"numa_pages_migrated",
#endif
#ifdef CONFIG_MIGRATION
	"pgmigrate_success",
	"pgmigrate_fail",
	"thp_migration_success",
	"thp_migration_fail",
	"thp_migration_split",
#endif

#ifdef CONFIG_HUGETLB_PAGE
	"htlb_buddy_alloc_success",
	"htlb_buddy_alloc_fail",
#endif
	"unevictable_pgs_culled",
	"unevictable_pgs_scanned",
	"unevictable_pgs_rescued",
	"unevictable_pgs_mlocked",
	"unevictable_pgs_munlocked",
	"unevictable_pgs_cleared",
	"unevictable_pgs_stranded",

#ifdef CONFIG_MEMORY_BALLOON
	"balloon_inflate",
	"balloon_deflate",
#ifdef CONFIG_BALLOON_COMPACTION
	"balloon_migrate",
#endif
#endif /* CONFIG_MEMORY_BALLOON */

#ifdef CONFIG_DEBUG_VM_VMACACHE
	"vmacache_find_calls",
	"vmacache_find_hits",
#endif
#ifdef CONFIG_SWAP
	"swap_ra",
	"swap_ra_hit",
#endif
#ifdef CONFIG_ZSWAP
	"zswpin",
	"zswpout",
#endif
	"direct_map_level2_splits",
	"direct_map_level3_splits",
#endif /* CONFIG_VM_EVENT_COUNTERS || CONFIG_MEMCG */
};
#endif /* CONFIG_PROC_FS || CONFIG_SYSFS || CONFIG_NUMA || CONFIG_MEMCG */

#if (defined(CONFIG_DEBUG_FS) && defined(CONFIG_COMPACTION)) || \
     defined(CONFIG_PROC_FS)
static void *frag_start(struct seq_file *m, loff_t *pos)
{
	pg_data_t *pgdat;
	loff_t node = *pos;

	for (pgdat = first_online_pgdat();
	     pgdat && node;
	     pgdat = next_online_pgdat(pgdat))
		--node;

	return pgdat;
}

static void *frag_next(struct seq_file *m, void *arg, loff_t *pos)
{
	pg_data_t *pgdat = (pg_data_t *)arg;

	(*pos)++;
	return next_online_pgdat(pgdat);
}

static void frag_stop(struct seq_file *m, void *arg)
{
}

/*
 * Walk zones in a node and print using a callback.
 * If @assert_populated is true, only use callback for zones that are populated.
 */
static void walk_zones_in_node(struct seq_file *m, pg_data_t *pgdat,
		bool assert_populated, bool nolock,
		void (*print)(struct seq_file *m, pg_data_t *, struct zone *))
{
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;
	unsigned long flags;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (assert_populated && !populated_zone(zone))
			continue;

		if (!nolock)
			spin_lock_irqsave(&zone->lock, flags);
		print(m, pgdat, zone);
		if (!nolock)
			spin_unlock_irqrestore(&zone->lock, flags);
	}
}
#endif



struct workqueue_struct *mm_percpu_wq;

void __init init_mm_internals(void)
{
	int ret __maybe_unused;

	mm_percpu_wq = alloc_workqueue("mm_percpu_wq", WQ_MEM_RECLAIM, 0);

	migrate_on_reclaim_init();
}

