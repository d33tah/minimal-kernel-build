#include <linux/mm.h>
#include "slab.h"
#include "internal.h"

static void init_one_lru(struct list_lru_one *l)
{
	INIT_LIST_HEAD(&l->list);
}

int __list_lru_init(struct list_lru *lru, bool memcg_aware,
		    struct lock_class_key *key)
{
	int i;

	lru->node = kcalloc(nr_node_ids, sizeof(*lru->node), GFP_KERNEL);
	if (!lru->node)
		return -ENOMEM;

	for_each_node(i) {
		spin_lock_init(&lru->node[i].lock);
		if (key)
			lockdep_set_class(&lru->node[i].lock, key);
		init_one_lru(&lru->node[i].lru);
	}

	return 0;
}
