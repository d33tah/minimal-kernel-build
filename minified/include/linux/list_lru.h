#ifndef _LRU_LIST_H
#define _LRU_LIST_H

#include <linux/list.h>
#include <linux/nodemask.h>
/* Inlined from shrinker.h */
struct shrink_control { gfp_t gfp_mask; int nid; unsigned long nr_to_scan; /* nr_scanned removed */ struct mem_cgroup *memcg; };
#define SHRINK_STOP (~0UL)
#define SHRINK_EMPTY (~0UL - 1)
struct shrinker {
	unsigned long (*count_objects)(struct shrinker *, struct shrink_control *sc);
	unsigned long (*scan_objects)(struct shrinker *, struct shrink_control *sc);
	/* batch, seeks, flags, list, nr_deferred removed - never read */
};
/* DEFAULT_SEEKS removed - never used */
/* SHRINKER_* flags removed - never read, only written */
/* SHRINKER_NONSLAB removed - never used */
/* shrinker functions removed - never called/defined */
#include <linux/xarray.h>

struct mem_cgroup;

enum lru_status {
	LRU_REMOVED,		 
	LRU_REMOVED_RETRY,	 
	LRU_ROTATE,		 
	LRU_SKIP,		 
	LRU_RETRY,		 
};

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

void list_lru_destroy(struct list_lru *lru);
int __list_lru_init(struct list_lru *lru, bool memcg_aware,
		    struct lock_class_key *key, struct shrinker *shrinker);

/* list_lru_init, list_lru_init_key removed - unused */
#define list_lru_init_memcg(lru, shrinker)		\
	__list_lru_init((lru), true, NULL, shrinker)

bool list_lru_add(struct list_lru *lru, struct list_head *item);

bool list_lru_del(struct list_lru *lru, struct list_head *item);

unsigned long list_lru_count_one(struct list_lru *lru,
				 int nid, struct mem_cgroup *memcg);

static inline unsigned long list_lru_shrink_count(struct list_lru *lru,
						  struct shrink_control *sc)
{
	return list_lru_count_one(lru, sc->nid, sc->memcg);
}

/* list_lru_isolate, list_lru_isolate_move inlined into single call sites */

typedef enum lru_status (*list_lru_walk_cb)(struct list_head *item,
		struct list_lru_one *list, spinlock_t *lock, void *cb_arg);

unsigned long list_lru_walk_one(struct list_lru *lru,
				int nid, struct mem_cgroup *memcg,
				list_lru_walk_cb isolate, void *cb_arg,
				unsigned long *nr_to_walk);

/* list_lru_shrink_walk removed - never called */


#endif
