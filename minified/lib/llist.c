#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/llist.h>


bool llist_add_batch(struct llist_node *new_first, struct llist_node *new_last,
		     struct llist_head *head)
{
	struct llist_node *first;

	do {
		new_last->next = first = READ_ONCE(head->first);
	} while (cmpxchg(&head->first, first, new_first) != first);

	return !first;
}

struct llist_node *llist_del_first(struct llist_head *head)
{
	struct llist_node *entry, *old_entry, *next;

	entry = smp_load_acquire(&head->first);
	for (;;) {
		if (entry == NULL)
			return NULL;
		old_entry = entry;
		next = READ_ONCE(entry->next);
		entry = cmpxchg(&head->first, old_entry, next);
		if (entry == old_entry)
			break;
	}

	return entry;
}

/* llist_reverse_order removed - unused in minimal kernel */
