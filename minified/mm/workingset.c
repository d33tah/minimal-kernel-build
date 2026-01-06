/* Minimal workingset - only keeping callback and init */
#include <linux/mm.h>
#include <linux/list_lru.h>
#include <linux/xarray.h>
/* workingset_refault/workingset_activation removed - calls removed */
void workingset_update_node(struct xa_node *node)
{
}
struct list_lru shadow_nodes;
/* workingset_init removed - list_lru_init hangs with low memory */
