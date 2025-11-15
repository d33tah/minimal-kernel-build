 
 

#include <linux/transport_class.h>
#include <linux/export.h>

 
int transport_class_register(struct transport_class *tclass)
{
	return 0;
}

void transport_class_unregister(struct transport_class *tclass)
{
}

int anon_transport_class_register(struct anon_transport_class *atc)
{
	return 0;
}

void anon_transport_class_unregister(struct anon_transport_class *atc)
{
}

void transport_setup_device(struct device *dev)
{
}

int transport_add_device(struct device *dev)
{
	return 0;
}

void transport_configure_device(struct device *dev)
{
}

void transport_remove_device(struct device *dev)
{
}

void transport_destroy_device(struct device *dev)
{
}
