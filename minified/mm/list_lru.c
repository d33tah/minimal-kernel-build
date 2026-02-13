/* --- 2026-02-08 08:40 --- */
/* Stubbed list_lru for minimal Hello World kernel - no memory reclaim needed */
#include <linux/list_lru.h>
#include <linux/slab.h>

bool list_lru_del(struct list_lru *lru, struct list_head *item)
{
	return false;
}

unsigned long list_lru_count_one(struct list_lru *lru, int nid,
				 struct mem_cgroup *memcg)
{
	return 0;
}

int __list_lru_init(struct list_lru *lru, bool memcg_aware,
		    struct lock_class_key *key, struct shrinker *shrinker)
{
	return 0;
}

void list_lru_destroy(struct list_lru *lru)
{
}
