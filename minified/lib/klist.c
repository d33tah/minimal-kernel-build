/* Stubbed - klist only needed for device driver matching which is stubbed */
#include <linux/device.h>

int klist_node_attached(struct klist_node *n)
{
	return (n->n_klist != NULL);
}

void klist_iter_init_node(struct klist *k, struct klist_iter *i,
			  struct klist_node *n)
{
	i->i_klist = k;
	i->i_cur = NULL;
}

void klist_iter_exit(struct klist_iter *i)
{
	i->i_cur = NULL;
}

struct klist_node *klist_next(struct klist_iter *i)
{
	return NULL;
}
