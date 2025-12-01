#ifndef _LINUX_SHRINKER_H
#define _LINUX_SHRINKER_H

struct shrink_control {
	gfp_t gfp_mask;

	 
	int nid;

	 
	unsigned long nr_to_scan;

	 
	unsigned long nr_scanned;

	 
	struct mem_cgroup *memcg;
};

#define SHRINK_STOP (~0UL)
#define SHRINK_EMPTY (~0UL - 1)
struct shrinker {
	unsigned long (*count_objects)(struct shrinker *,
				       struct shrink_control *sc);
	unsigned long (*scan_objects)(struct shrinker *,
				      struct shrink_control *sc);

	long batch;	 
	int seeks;	 
	unsigned flags;

	 
	struct list_head list;
	 
	atomic_long_t *nr_deferred;
};
#define DEFAULT_SEEKS 2  

#define SHRINKER_REGISTERED	(1 << 0)
#define SHRINKER_NUMA_AWARE	(1 << 1)
#define SHRINKER_MEMCG_AWARE	(1 << 2)
#define SHRINKER_NONSLAB	(1 << 3)

extern int prealloc_shrinker(struct shrinker *shrinker);
extern void register_shrinker_prepared(struct shrinker *shrinker);
extern int register_shrinker(struct shrinker *shrinker);
extern void unregister_shrinker(struct shrinker *shrinker);
extern void free_prealloced_shrinker(struct shrinker *shrinker);
extern void synchronize_shrinkers(void);
#endif
