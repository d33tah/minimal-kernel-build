#ifndef _LRU_LIST_H
#define _LRU_LIST_H

#include <linux/list.h>
#include <linux/nodemask.h>
struct shrink_control { gfp_t gfp_mask; int nid; unsigned long nr_to_scan; /* nr_scanned removed */ struct mem_cgroup *memcg; };
struct shrinker {
};
#include <linux/xarray.h>

struct mem_cgroup;

struct list_lru_one {
	struct list_head	list;
	 
	long			nr_items;
};

struct list_lru_node {
	 
	spinlock_t		lock;
	 
	struct list_lru_one	lru;
	long			nr_items;
} ____cacheline_aligned_in_smp;

struct list_lru {
	struct list_lru_node	*node;
};

static inline void list_lru_destroy(struct list_lru *lru) {}
static inline int __list_lru_init(struct list_lru *lru, bool memcg_aware,
		    struct lock_class_key *key, struct shrinker *shrinker) { return 0; }

#define list_lru_init_memcg(lru, shrinker)		\
	__list_lru_init((lru), true, NULL, shrinker)

static inline bool list_lru_del(struct list_lru *lru, struct list_head *item) { return false; }

#endif
