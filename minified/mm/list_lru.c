#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/list_lru.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/memcontrol.h>
#include "slab.h"
#include "internal.h"

/* list_lru_register/unregister removed - empty stubs */

/* lru_shrinker_id removed - set_shrinker_bit is empty stub */

static inline struct list_lru_one *list_lru_from_memcg_idx(struct list_lru *lru,
							   int nid, int idx)
{
	return &lru->node[nid].lru;
}

static inline struct list_lru_one *
list_lru_from_kmem(struct list_lru *lru, int nid, void *ptr,
		   struct mem_cgroup **memcg_ptr)
{
	if (memcg_ptr)
		*memcg_ptr = NULL;
	return &lru->node[nid].lru;
}

bool list_lru_add(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct list_lru_node *nlru = &lru->node[nid];
	struct mem_cgroup *memcg;
	struct list_lru_one *l;

	spin_lock(&nlru->lock);
	if (list_empty(item)) {
		l = list_lru_from_kmem(lru, nid, item, &memcg);
		list_add_tail(item, &l->list);
		l->nr_items++;
		/* set_shrinker_bit call removed - empty stub */
		nlru->nr_items++;
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
	struct list_lru_one *l;

	spin_lock(&nlru->lock);
	if (!list_empty(item)) {
		l = list_lru_from_kmem(lru, nid, item, NULL);
		list_del_init(item);
		l->nr_items--;
		nlru->nr_items--;
		spin_unlock(&nlru->lock);
		return true;
	}
	spin_unlock(&nlru->lock);
	return false;
}

/* list_lru_isolate, list_lru_isolate_move inlined into single call sites */

unsigned long list_lru_count_one(struct list_lru *lru, int nid,
				 struct mem_cgroup *memcg)
{
	struct list_lru_one *l;
	long count;

	rcu_read_lock();
	l = list_lru_from_memcg_idx(lru, nid, memcg_kmem_id(memcg));
	count = l ? READ_ONCE(l->nr_items) : 0;
	rcu_read_unlock();

	if (unlikely(count < 0))
		count = 0;

	return count;
}

/* __list_lru_walk_one inlined into list_lru_walk_one */

unsigned long list_lru_walk_one(struct list_lru *lru, int nid,
				struct mem_cgroup *memcg,
				list_lru_walk_cb isolate, void *cb_arg,
				unsigned long *nr_to_walk)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;
	struct list_head *item, *n;
	unsigned long isolated = 0;

	spin_lock(&nlru->lock);
restart:
	l = list_lru_from_memcg_idx(lru, nid, memcg_kmem_id(memcg));
	if (!l)
		goto out;

	list_for_each_safe(item, n, &l->list) {
		enum lru_status ret;

		if (!*nr_to_walk)
			break;
		--*nr_to_walk;

		ret = isolate(item, l, &nlru->lock, cb_arg);
		switch (ret) {
		case LRU_REMOVED_RETRY:
			assert_spin_locked(&nlru->lock);
			fallthrough;
		case LRU_REMOVED:
			isolated++;
			nlru->nr_items--;
			if (ret == LRU_REMOVED_RETRY)
				goto restart;
			break;
		case LRU_ROTATE:
			list_move_tail(item, &l->list);
			break;
		case LRU_SKIP:
			break;
		case LRU_RETRY:
			assert_spin_locked(&nlru->lock);
			goto restart;
		default:
			BUG();
		}
	}
out:
	spin_unlock(&nlru->lock);
	return isolated;
}

/* memcg_init_list_lru, memcg_destroy_list_lru removed - empty stubs */

int __list_lru_init(struct list_lru *lru, bool memcg_aware,
		    struct lock_class_key *key, struct shrinker *shrinker)
{
	lru->node = kcalloc(nr_node_ids, sizeof(*lru->node), GFP_KERNEL);
	if (!lru->node)
		return -ENOMEM;

	/* for_each_node simplified - single node */
	spin_lock_init(&lru->node[0].lock);
	/* lockdep_set_class removed - empty stub */
	INIT_LIST_HEAD(&lru->node[0].lru.list);
	lru->node[0].lru.nr_items = 0;

	/* memcg_init_list_lru, list_lru_register removed - empty stubs */
	return 0;
}

void list_lru_destroy(struct list_lru *lru)
{
	if (!lru->node)
		return;
	/* list_lru_unregister, memcg_destroy_list_lru removed - empty stubs */
	kfree(lru->node);
	lru->node = NULL;
}
