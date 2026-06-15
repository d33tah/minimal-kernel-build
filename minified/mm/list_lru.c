#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/list_lru.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/memcontrol.h>
#include "slab.h"
#include "internal.h"

bool list_lru_add(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct list_lru_node *nlru = &lru->node[nid];

	spin_lock(&nlru->lock);
	if (list_empty(item)) {
		list_add_tail(item, &nlru->lru.list);
		spin_unlock(&nlru->lock);
		return true;
	}
	spin_unlock(&nlru->lock);
	return false;
}

bool list_lru_del(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct list_lru_node *nlru = &lru->node[nid];

	spin_lock(&nlru->lock);
	if (!list_empty(item)) {
		list_del_init(item);
		spin_unlock(&nlru->lock);
		return true;
	}
	spin_unlock(&nlru->lock);
	return false;
}

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

void list_lru_destroy(struct list_lru *lru)
{

	if (!lru->node)
		return;

	kfree(lru->node);
	lru->node = NULL;

}
