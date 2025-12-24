/* Minimal includes for workingset stubs */
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/list_lru.h>
#include <linux/xarray.h>
void workingset_refault(struct folio *folio, void *shadow)
{
}
void workingset_activation(struct folio *folio)
{
}
void workingset_update_node(struct xa_node *node)
{
}
struct list_lru shadow_nodes;
static int __init workingset_init(void)
{
	return list_lru_init(&shadow_nodes);
}
module_init(workingset_init);
