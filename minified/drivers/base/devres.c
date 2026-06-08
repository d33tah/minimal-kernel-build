
#include <linux/device.h>
#include <linux/kernel.h>

/* devres_for_each_res, devres_find, devres_get, devres_remove,
   devres_destroy, devres_release removed - never called.
   __devres_alloc_node, devres_free, devres_add, devm_kstrdup,
   devm_kasprintf removed - never called. */

int devres_release_all(struct device *dev)
{
	return 0;
}
