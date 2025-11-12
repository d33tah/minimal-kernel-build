// SPDX-License-Identifier: GPL-2.0-only
/*
 * transport_class.c - transport class support - STUBBED for minimal kernel
 */

#include <linux/transport_class.h>
#include <linux/export.h>

/* Stub functions - all return success or do nothing */
int transport_class_register(struct transport_class *tclass)
{
	return 0;
}
EXPORT_SYMBOL_GPL(transport_class_register);

void transport_class_unregister(struct transport_class *tclass)
{
}
EXPORT_SYMBOL_GPL(transport_class_unregister);

int anon_transport_class_register(struct anon_transport_class *atc)
{
	return 0;
}
EXPORT_SYMBOL_GPL(anon_transport_class_register);

void anon_transport_class_unregister(struct anon_transport_class *atc)
{
}
EXPORT_SYMBOL_GPL(anon_transport_class_unregister);

void transport_setup_device(struct device *dev)
{
}
EXPORT_SYMBOL_GPL(transport_setup_device);

int transport_add_device(struct device *dev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(transport_add_device);

void transport_configure_device(struct device *dev)
{
}
EXPORT_SYMBOL_GPL(transport_configure_device);

void transport_remove_device(struct device *dev)
{
}
EXPORT_SYMBOL_GPL(transport_remove_device);

void transport_destroy_device(struct device *dev)
{
}
EXPORT_SYMBOL_GPL(transport_destroy_device);
