/* klist.h inlined via device.h */
#include <linux/device.h>
#include <linux/sched.h>

#define KNODE_DEAD 1LU
#define KNODE_KLIST_MASK ~KNODE_DEAD

static bool knode_dead(struct klist_node *knode)
{
	return (unsigned long)knode->n_klist & KNODE_DEAD;
}

/* knode_kill inlined - set KNODE_DEAD bit in n_klist */

static void klist_release(struct kref *kref)
{
	struct klist_node *n = container_of(kref, struct klist_node, n_ref);

	WARN_ON(!knode_dead(n));
	list_del(&n->n_node);
	n->n_klist = NULL;
}

static int klist_dec_and_del(struct klist_node *n)
{
	return kref_put(&n->n_ref, klist_release);
}

static void klist_put(struct klist_node *n, bool kill)
{
	struct klist *k =
		(struct klist *)((unsigned long)n->n_klist & KNODE_KLIST_MASK);
	void (*put)(struct klist_node *) = k->put;

	spin_lock(&k->k_lock);
	if (kill) {
		WARN_ON(knode_dead(n));
		*(unsigned long *)&n->n_klist |= KNODE_DEAD;
	}
	if (!klist_dec_and_del(n))
		put = NULL;
	spin_unlock(&k->k_lock);
	if (put)
		put(n);
}

int klist_node_attached(struct klist_node *n)
{
	return (n->n_klist != NULL);
}

void klist_iter_init_node(struct klist *k, struct klist_iter *i,
			  struct klist_node *n)
{
	i->i_klist = k;
	i->i_cur = NULL;
	if (n && kref_get_unless_zero(&n->n_ref))
		i->i_cur = n;
}

void klist_iter_exit(struct klist_iter *i)
{
	if (i->i_cur) {
		klist_put(i->i_cur, false);
		i->i_cur = NULL;
	}
}

static struct klist_node *to_klist_node(struct list_head *n)
{
	return container_of(n, struct klist_node, n_node);
}

struct klist_node *klist_next(struct klist_iter *i)
{
	void (*put)(struct klist_node *) = i->i_klist->put;
	struct klist_node *last = i->i_cur;
	struct klist_node *next;
	unsigned long flags;

	spin_lock_irqsave(&i->i_klist->k_lock, flags);

	if (last) {
		next = to_klist_node(last->n_node.next);
		if (!klist_dec_and_del(last))
			put = NULL;
	} else
		next = to_klist_node(i->i_klist->k_list.next);

	i->i_cur = NULL;
	while (next != to_klist_node(&i->i_klist->k_list)) {
		if (likely(!knode_dead(next))) {
			kref_get(&next->n_ref);
			i->i_cur = next;
			break;
		}
		next = to_klist_node(next->n_node.next);
	}

	spin_unlock_irqrestore(&i->i_klist->k_lock, flags);

	if (put && last)
		put(last);
	return i->i_cur;
}
