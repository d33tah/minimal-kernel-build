 
 

#include <linux/attribute_container.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct attribute_container *
attribute_container_classdev_to_container(struct device *classdev)
{
	return NULL;
}

int attribute_container_register(struct attribute_container *cont)
{
	return 0;
}

int attribute_container_unregister(struct attribute_container *cont)
{
	return 0;
}

struct device *
attribute_container_find_class_device(struct attribute_container *cont,
				      struct device *dev)
{
	return NULL;
}
