/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RCULIST_BL_H
#define _LINUX_RCULIST_BL_H

#include <linux/bit_spinlock.h>
#include <linux/rcupdate.h>

#define LIST_BL_LOCKMASK 0UL

struct hlist_bl_head {
	struct hlist_bl_node *first;
};

struct hlist_bl_node {
	struct hlist_bl_node *next, **pprev;
};
#define INIT_HLIST_BL_HEAD(ptr) ((ptr)->first = NULL)

static inline void INIT_HLIST_BL_NODE(struct hlist_bl_node *h)
{
	h->next = NULL;
	h->pprev = NULL;
}

#define hlist_bl_entry(ptr, type, member) container_of(ptr, type, member)

static inline bool hlist_bl_unhashed(const struct hlist_bl_node *h)
{
	return !h->pprev;
}

static inline struct hlist_bl_node *hlist_bl_first(struct hlist_bl_head *h)
{
	return (struct hlist_bl_node *)((unsigned long)h->first &
					~LIST_BL_LOCKMASK);
}

static inline void __hlist_bl_del(struct hlist_bl_node *n)
{
	struct hlist_bl_node *next = n->next;
	struct hlist_bl_node **pprev = n->pprev;

	WRITE_ONCE(*pprev, (struct hlist_bl_node *)((unsigned long)next |
						    ((unsigned long)*pprev &
						     LIST_BL_LOCKMASK)));
	if (next)
		next->pprev = pprev;
}

static inline void hlist_bl_lock(struct hlist_bl_head *b)
{
	bit_spin_lock(0, (unsigned long *)b);
}

static inline void hlist_bl_unlock(struct hlist_bl_head *b)
{
	__bit_spin_unlock(0, (unsigned long *)b);
}

static inline bool hlist_bl_is_locked(struct hlist_bl_head *b)
{
	return bit_spin_is_locked(0, (unsigned long *)b);
}

/* RCU-specific functions */
static inline void hlist_bl_set_first_rcu(struct hlist_bl_head *h,
					  struct hlist_bl_node *n)
{
	rcu_assign_pointer(h->first,
			   (struct hlist_bl_node *)((unsigned long)n |
						    LIST_BL_LOCKMASK));
}

static inline struct hlist_bl_node *hlist_bl_first_rcu(struct hlist_bl_head *h)
{
	return (struct hlist_bl_node *)((unsigned long)rcu_dereference_check(
						h->first,
						hlist_bl_is_locked(h)) &
					~LIST_BL_LOCKMASK);
}

/**
 * hlist_bl_add_head_rcu
 * @n: the element to add to the hash list.
 * @h: the list to add to.
 *
 * Description:
 * Adds the specified element to the specified hlist_bl,
 * while permitting racing traversals.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as hlist_bl_add_head_rcu()
 * or hlist_bl_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * hlist_bl_for_each_entry_rcu(), used to prevent memory-consistency
 * problems on Alpha CPUs.  Regardless of the type of CPU, the
 * list-traversal primitive must be guarded by rcu_read_lock().
 */
static inline void hlist_bl_add_head_rcu(struct hlist_bl_node *n,
					 struct hlist_bl_head *h)
{
	struct hlist_bl_node *first;

	/* don't need hlist_bl_first_rcu because we're under lock */
	first = hlist_bl_first(h);

	n->next = first;
	if (first)
		first->pprev = &n->next;
	n->pprev = &h->first;

	/* need _rcu because we can have concurrent lock free readers */
	hlist_bl_set_first_rcu(h, n);
}
/**
 * hlist_bl_for_each_entry_rcu - iterate over rcu list of given type
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct hlist_bl_node to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the hlist_bl_node within the struct.
 *
 */
#define hlist_bl_for_each_entry_rcu(tpos, pos, head, member)            \
	for (pos = hlist_bl_first_rcu(head);                            \
	     pos && ({                                                  \
		     tpos = hlist_bl_entry(pos, typeof(*tpos), member); \
		     1;                                                 \
	     });                                                        \
	     pos = rcu_dereference_raw(pos->next))

#endif
