// SPDX-License-Identifier: GPL-2.0
/*
 * Componentized device handling - STUBBED for minimal kernel
 */

#include <linux/component.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

/* Stub functions - all return success or NULL */
int component_compare_of(struct device *dev, void *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(component_compare_of);

void component_release_of(struct device *dev, void *data)
{
}
EXPORT_SYMBOL_GPL(component_release_of);

int component_compare_dev(struct device *dev, void *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(component_compare_dev);

int component_compare_dev_name(struct device *dev, void *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(component_compare_dev_name);

void component_match_add_release(struct device *master,
	struct component_match **matchptr,
	void (*release)(struct device *, void *),
	int (*compare)(struct device *, void *), void *compare_data)
{
}
EXPORT_SYMBOL(component_match_add_release);

void component_match_add_typed(struct device *master,
	struct component_match **matchptr,
	int (*compare_typed)(struct device *, int, void *), void *compare_data)
{
}
EXPORT_SYMBOL(component_match_add_typed);

int component_master_add_with_match(struct device *parent,
	const struct component_master_ops *ops,
	struct component_match *match)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(component_master_add_with_match);

void component_master_del(struct device *parent,
	const struct component_master_ops *ops)
{
}
EXPORT_SYMBOL_GPL(component_master_del);

void component_unbind_all(struct device *parent, void *data)
{
}
EXPORT_SYMBOL_GPL(component_unbind_all);

int component_bind_all(struct device *parent, void *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(component_bind_all);

int component_add_typed(struct device *dev, const struct component_ops *ops, int subcomponent)
{
	return 0;
}
EXPORT_SYMBOL_GPL(component_add_typed);

int component_add(struct device *dev, const struct component_ops *ops)
{
	return component_add_typed(dev, ops, 0);
}
EXPORT_SYMBOL_GPL(component_add);

void component_del(struct device *dev, const struct component_ops *ops)
{
}
EXPORT_SYMBOL_GPL(component_del);
