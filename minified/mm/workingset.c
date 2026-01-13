/* Minimal workingset - only keeping shadow_nodes variable */
#include <linux/list_lru.h>
/* workingset_refault/workingset_activation/workingset_update_node removed */
struct list_lru shadow_nodes;
