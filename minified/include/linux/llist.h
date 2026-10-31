#ifndef LLIST_H
#define LLIST_H

struct llist_head {
	struct llist_node *first;
};

struct llist_node {
	struct llist_node *next;
};

static inline void init_llist_head(struct llist_head *list)
{
	list->first = NULL;
}

#define member_address_is_nonnull(ptr, member) \
	((uintptr_t)(ptr) + offsetof(typeof(*(ptr)), member) != 0)

#endif
