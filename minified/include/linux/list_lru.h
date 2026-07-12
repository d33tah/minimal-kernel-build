#ifndef _LRU_LIST_H
#define _LRU_LIST_H

#include <linux/list.h>
#include <linux/nodemask.h>
#include <linux/xarray.h>


struct list_lru_one {
	struct list_head	list;
};

struct list_lru_node {
	spinlock_t		lock;
	struct list_lru_one	lru;
} ____cacheline_aligned_in_smp;

struct list_lru {
	struct list_lru_node	*node;
};

int __list_lru_init(struct list_lru *lru, bool memcg_aware, struct lock_class_key *key);

#define list_lru_init(lru)					__list_lru_init((lru), false, NULL)
#define list_lru_init_memcg(lru, shrinker)			__list_lru_init((lru), true, NULL)

bool list_lru_add(struct list_lru *lru, struct list_head *item);

bool list_lru_del(struct list_lru *lru, struct list_head *item);

#endif
