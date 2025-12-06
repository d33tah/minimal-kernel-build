#ifndef _LINUX_RCULIST_BL_H
#define _LINUX_RCULIST_BL_H

#include <linux/list.h>
#include <linux/bit_spinlock.h>
#include <linux/rcupdate.h>

/* Inlined from list_bl.h */
#define LIST_BL_LOCKMASK	0UL

#define LIST_BL_BUG_ON(x)


struct hlist_bl_head {
	struct hlist_bl_node *first;
};

struct hlist_bl_node {
	struct hlist_bl_node *next, **pprev;
};
#define INIT_HLIST_BL_HEAD(ptr) \
	((ptr)->first = NULL)

static inline void INIT_HLIST_BL_NODE(struct hlist_bl_node *h)
{
	h->next = NULL;
	h->pprev = NULL;
}

#define hlist_bl_entry(ptr, type, member) container_of(ptr,type,member)

static inline bool  hlist_bl_unhashed(const struct hlist_bl_node *h)
{
	return !h->pprev;
}

static inline struct hlist_bl_node *hlist_bl_first(struct hlist_bl_head *h)
{
	return (struct hlist_bl_node *)
		((unsigned long)h->first & ~LIST_BL_LOCKMASK);
}

static inline void hlist_bl_set_first(struct hlist_bl_head *h,
					struct hlist_bl_node *n)
{
	LIST_BL_BUG_ON((unsigned long)n & LIST_BL_LOCKMASK);
	LIST_BL_BUG_ON(((unsigned long)h->first & LIST_BL_LOCKMASK) !=
							LIST_BL_LOCKMASK);
	h->first = (struct hlist_bl_node *)((unsigned long)n | LIST_BL_LOCKMASK);
}

static inline bool hlist_bl_empty(const struct hlist_bl_head *h)
{
	return !((unsigned long)READ_ONCE(h->first) & ~LIST_BL_LOCKMASK);
}

static inline void hlist_bl_add_head(struct hlist_bl_node *n,
					struct hlist_bl_head *h)
{
	struct hlist_bl_node *first = hlist_bl_first(h);

	n->next = first;
	if (first)
		first->pprev = &n->next;
	n->pprev = &h->first;
	hlist_bl_set_first(h, n);
}

static inline void hlist_bl_add_before(struct hlist_bl_node *n,
				       struct hlist_bl_node *next)
{
	struct hlist_bl_node **pprev = next->pprev;

	n->pprev = pprev;
	n->next = next;
	next->pprev = &n->next;


	WRITE_ONCE(*pprev,
		   (struct hlist_bl_node *)
			((uintptr_t)n | ((uintptr_t)*pprev & LIST_BL_LOCKMASK)));
}

static inline void hlist_bl_add_behind(struct hlist_bl_node *n,
				       struct hlist_bl_node *prev)
{
	n->next = prev->next;
	n->pprev = &prev->next;
	prev->next = n;

	if (n->next)
		n->next->pprev = &n->next;
}

static inline void __hlist_bl_del(struct hlist_bl_node *n)
{
	struct hlist_bl_node *next = n->next;
	struct hlist_bl_node **pprev = n->pprev;

	LIST_BL_BUG_ON((unsigned long)n & LIST_BL_LOCKMASK);


	WRITE_ONCE(*pprev,
		   (struct hlist_bl_node *)
			((unsigned long)next |
			 ((unsigned long)*pprev & LIST_BL_LOCKMASK)));
	if (next)
		next->pprev = pprev;
}

static inline void hlist_bl_del(struct hlist_bl_node *n)
{
	__hlist_bl_del(n);
	n->next = LIST_POISON1;
	n->pprev = LIST_POISON2;
}

static inline void hlist_bl_del_init(struct hlist_bl_node *n)
{
	if (!hlist_bl_unhashed(n)) {
		__hlist_bl_del(n);
		INIT_HLIST_BL_NODE(n);
	}
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

#define hlist_bl_for_each_entry(tpos, pos, head, member)		\
	for (pos = hlist_bl_first(head);				\
	     pos &&							\
		({ tpos = hlist_bl_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

#define hlist_bl_for_each_entry_safe(tpos, pos, n, head, member)	 \
	for (pos = hlist_bl_first(head);				 \
	     pos && ({ n = pos->next; 1; }) && 				 \
		({ tpos = hlist_bl_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = n)

/* End of inlined list_bl.h content */

/* RCU-specific functions */
static inline void hlist_bl_set_first_rcu(struct hlist_bl_head *h,
					struct hlist_bl_node *n)
{
	LIST_BL_BUG_ON((unsigned long)n & LIST_BL_LOCKMASK);
	LIST_BL_BUG_ON(((unsigned long)h->first & LIST_BL_LOCKMASK) !=
							LIST_BL_LOCKMASK);
	rcu_assign_pointer(h->first,
		(struct hlist_bl_node *)((unsigned long)n | LIST_BL_LOCKMASK));
}

static inline struct hlist_bl_node *hlist_bl_first_rcu(struct hlist_bl_head *h)
{
	return (struct hlist_bl_node *)
		((unsigned long)rcu_dereference_check(h->first, hlist_bl_is_locked(h)) & ~LIST_BL_LOCKMASK);
}

static inline void hlist_bl_del_rcu(struct hlist_bl_node *n)
{
	__hlist_bl_del(n);
	n->pprev = LIST_POISON2;
}

static inline void hlist_bl_add_head_rcu(struct hlist_bl_node *n,
					struct hlist_bl_head *h)
{
	struct hlist_bl_node *first;


	first = hlist_bl_first(h);

	n->next = first;
	if (first)
		first->pprev = &n->next;
	n->pprev = &h->first;


	hlist_bl_set_first_rcu(h, n);
}
#define hlist_bl_for_each_entry_rcu(tpos, pos, head, member)		\
	for (pos = hlist_bl_first_rcu(head);				\
		pos &&							\
		({ tpos = hlist_bl_entry(pos, typeof(*tpos), member); 1; }); \
		pos = rcu_dereference_raw(pos->next))

#endif
