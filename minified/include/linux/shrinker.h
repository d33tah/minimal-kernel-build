#ifndef _LINUX_SHRINKER_H
#define _LINUX_SHRINKER_H
struct shrink_control { gfp_t gfp_mask; int nid; unsigned long nr_to_scan; /* nr_scanned removed */ struct mem_cgroup *memcg; };
#define SHRINK_STOP (~0UL)
#define SHRINK_EMPTY (~0UL - 1)
struct shrinker {
	unsigned long (*count_objects)(struct shrinker *, struct shrink_control *sc);
	unsigned long (*scan_objects)(struct shrinker *, struct shrink_control *sc);
	long batch; int seeks; unsigned flags;
	/* list, nr_deferred removed - never accessed */
};
#define DEFAULT_SEEKS 2
/* SHRINKER_REGISTERED removed - never used */
#define SHRINKER_NUMA_AWARE (1 << 1)
#define SHRINKER_MEMCG_AWARE (1 << 2)
/* SHRINKER_NONSLAB removed - never used */
/* shrinker functions removed - never called/defined */
#endif
