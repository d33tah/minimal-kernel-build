 
 

#include <linux/async.h>
#include <linux/device/bus.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include "base.h"
#include "power/power.h"

 
static struct kset *system_kset;

#define to_bus_attr(_attr) container_of(_attr, struct bus_attribute, attr)

 

#define to_drv_attr(_attr) container_of(_attr, struct driver_attribute, attr)

#define DRIVER_ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store) \
	struct driver_attribute driver_attr_##_name =		\
		__ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store)


static struct bus_type *bus_get(struct bus_type *bus)
{
	if (bus) {
		kset_get(&bus->p->subsys);
		return bus;
	}
	return NULL;
}

static void bus_put(struct bus_type *bus)
{
	if (bus)
		kset_put(&bus->p->subsys);
}

static ssize_t drv_attr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct driver_attribute *drv_attr = to_drv_attr(attr);
	struct driver_private *drv_priv = to_driver(kobj);
	ssize_t ret = -EIO;

	if (drv_attr->show)
		ret = drv_attr->show(drv_priv->driver, buf);
	return ret;
}

static ssize_t drv_attr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct driver_attribute *drv_attr = to_drv_attr(attr);
	struct driver_private *drv_priv = to_driver(kobj);
	ssize_t ret = -EIO;

	if (drv_attr->store)
		ret = drv_attr->store(drv_priv->driver, buf, count);
	return ret;
}

static const struct sysfs_ops driver_sysfs_ops = {
	.show	= drv_attr_show,
	.store	= drv_attr_store,
};

static void driver_release(struct kobject *kobj)
{
	struct driver_private *drv_priv = to_driver(kobj);

	kfree(drv_priv);
}

static struct kobj_type driver_ktype = {
	.sysfs_ops	= &driver_sysfs_ops,
	.release	= driver_release,
};

 
static ssize_t bus_attr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct bus_attribute *bus_attr = to_bus_attr(attr);
	struct subsys_private *subsys_priv = to_subsys_private(kobj);
	ssize_t ret = 0;

	if (bus_attr->show)
		ret = bus_attr->show(subsys_priv->bus, buf);
	return ret;
}

static ssize_t bus_attr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct bus_attribute *bus_attr = to_bus_attr(attr);
	struct subsys_private *subsys_priv = to_subsys_private(kobj);
	ssize_t ret = 0;

	if (bus_attr->store)
		ret = bus_attr->store(subsys_priv->bus, buf, count);
	return ret;
}

static const struct sysfs_ops bus_sysfs_ops = {
	.show	= bus_attr_show,
	.store	= bus_attr_store,
};

/* Stubbed: bus_create_file not used externally */
int bus_create_file(struct bus_type *bus, struct bus_attribute *attr) { return 0; }

/* Stubbed: bus_remove_file not used externally */
void bus_remove_file(struct bus_type *bus, struct bus_attribute *attr) { }

static void bus_release(struct kobject *kobj)
{
	struct subsys_private *priv = to_subsys_private(kobj);
	struct bus_type *bus = priv->bus;

	kfree(priv);
	bus->p = NULL;
}

static struct kobj_type bus_ktype = {
	.sysfs_ops	= &bus_sysfs_ops,
	.release	= bus_release,
};

static int bus_uevent_filter(struct kobject *kobj)
{
	const struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &bus_ktype)
		return 1;
	return 0;
}

static const struct kset_uevent_ops bus_uevent_ops = {
	.filter = bus_uevent_filter,
};

static struct kset *bus_kset;

 
static ssize_t unbind_store(struct device_driver *drv, const char *buf,
			    size_t count)
{
	/* Stub: sysfs driver unbind not needed for minimal kernel */
	return -ENOSYS;
}
static DRIVER_ATTR_IGNORE_LOCKDEP(unbind, 0200, NULL, unbind_store);

 
static ssize_t bind_store(struct device_driver *drv, const char *buf,
			  size_t count)
{
	/* Stub: sysfs driver bind not needed for minimal kernel */
	return -ENOSYS;
}
static DRIVER_ATTR_IGNORE_LOCKDEP(bind, 0200, NULL, bind_store);

/* Stub: drivers_autoprobe simplified for minimal kernel */
static ssize_t drivers_autoprobe_show(struct bus_type *bus, char *buf)
{
	return sysfs_emit(buf, "1\n");
}

static ssize_t drivers_autoprobe_store(struct bus_type *bus,
				       const char *buf, size_t count)
{
	return count;
}

/* Stubbed: drivers_probe_store relies on bus_rescan_devices_helper */
static ssize_t drivers_probe_store(struct bus_type *bus,
				   const char *buf, size_t count)
{
	return -ENOSYS;
}

static struct device *next_device(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct device *dev = NULL;
	struct device_private *dev_prv;

	if (n) {
		dev_prv = to_device_private_bus(n);
		dev = dev_prv->device;
	}
	return dev;
}

 
int bus_for_each_dev(struct bus_type *bus, struct device *start,
		     void *data, int (*fn)(struct device *, void *))
{
	struct klist_iter i;
	struct device *dev;
	int error = 0;

	if (!bus || !bus->p)
		return -EINVAL;

	klist_iter_init_node(&bus->p->klist_devices, &i,
			     (start ? &start->p->knode_bus : NULL));
	while (!error && (dev = next_device(&i)))
		error = fn(dev, data);
	klist_iter_exit(&i);
	return error;
}

 
struct device *bus_find_device(struct bus_type *bus,
			       struct device *start, const void *data,
			       int (*match)(struct device *dev, const void *data))
{
	struct klist_iter i;
	struct device *dev;

	if (!bus || !bus->p)
		return NULL;

	klist_iter_init_node(&bus->p->klist_devices, &i,
			     (start ? &start->p->knode_bus : NULL));
	while ((dev = next_device(&i)))
		if (match(dev, data) && get_device(dev))
			break;
	klist_iter_exit(&i);
	return dev;
}

/* Stubbed: subsys_find_device_by_id not used externally */
struct device *subsys_find_device_by_id(struct bus_type *subsys, unsigned int id,
					struct device *hint) { return NULL; }

static struct device_driver *next_driver(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct driver_private *drv_priv;

	if (n) {
		drv_priv = container_of(n, struct driver_private, knode_bus);
		return drv_priv->driver;
	}
	return NULL;
}

 
int bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *))
{
	struct klist_iter i;
	struct device_driver *drv;
	int error = 0;

	if (!bus)
		return -EINVAL;

	klist_iter_init_node(&bus->p->klist_drivers, &i,
			     start ? &start->p->knode_bus : NULL);
	while ((drv = next_driver(&i)) && !error)
		error = fn(drv, data);
	klist_iter_exit(&i);
	return error;
}

 
int bus_add_device(struct device *dev)
{
	struct bus_type *bus = bus_get(dev->bus);
	int error = 0;

	if (bus) {
		error = device_add_groups(dev, bus->dev_groups);
		if (error)
			goto out_put;
		error = sysfs_create_link(&bus->p->devices_kset->kobj,
						&dev->kobj, dev_name(dev));
		if (error)
			goto out_groups;
		error = sysfs_create_link(&dev->kobj,
				&dev->bus->p->subsys.kobj, "subsystem");
		if (error)
			goto out_subsys;
		klist_add_tail(&dev->p->knode_bus, &bus->p->klist_devices);
	}
	return 0;

out_subsys:
	sysfs_remove_link(&bus->p->devices_kset->kobj, dev_name(dev));
out_groups:
	device_remove_groups(dev, bus->dev_groups);
out_put:
	bus_put(dev->bus);
	return error;
}

 
void bus_probe_device(struct device *dev)
{
	struct bus_type *bus = dev->bus;
	struct subsys_interface *sif;

	if (!bus)
		return;

	if (bus->p->drivers_autoprobe)
		device_initial_probe(dev);

	mutex_lock(&bus->p->mutex);
	list_for_each_entry(sif, &bus->p->interfaces, node)
		if (sif->add_dev)
			sif->add_dev(dev, sif);
	mutex_unlock(&bus->p->mutex);
}

 
void bus_remove_device(struct device *dev)
{
	struct bus_type *bus = dev->bus;
	struct subsys_interface *sif;

	if (!bus)
		return;

	mutex_lock(&bus->p->mutex);
	list_for_each_entry(sif, &bus->p->interfaces, node)
		if (sif->remove_dev)
			sif->remove_dev(dev, sif);
	mutex_unlock(&bus->p->mutex);

	sysfs_remove_link(&dev->kobj, "subsystem");
	sysfs_remove_link(&dev->bus->p->devices_kset->kobj,
			  dev_name(dev));
	device_remove_groups(dev, dev->bus->dev_groups);
	if (klist_node_attached(&dev->p->knode_bus))
		klist_del(&dev->p->knode_bus);

	device_release_driver(dev);
	bus_put(dev->bus);
}

static int __must_check add_bind_files(struct device_driver *drv)
{
	int ret;

	ret = driver_create_file(drv, &driver_attr_unbind);
	if (ret == 0) {
		ret = driver_create_file(drv, &driver_attr_bind);
		if (ret)
			driver_remove_file(drv, &driver_attr_unbind);
	}
	return ret;
}

static void remove_bind_files(struct device_driver *drv)
{
	driver_remove_file(drv, &driver_attr_bind);
	driver_remove_file(drv, &driver_attr_unbind);
}

static BUS_ATTR_WO(drivers_probe);
static BUS_ATTR_RW(drivers_autoprobe);

static int add_probe_files(struct bus_type *bus)
{
	int retval;

	retval = bus_create_file(bus, &bus_attr_drivers_probe);
	if (retval)
		goto out;

	retval = bus_create_file(bus, &bus_attr_drivers_autoprobe);
	if (retval)
		bus_remove_file(bus, &bus_attr_drivers_probe);
out:
	return retval;
}

static void remove_probe_files(struct bus_type *bus)
{
	bus_remove_file(bus, &bus_attr_drivers_autoprobe);
	bus_remove_file(bus, &bus_attr_drivers_probe);
}

static ssize_t uevent_store(struct device_driver *drv, const char *buf,
			    size_t count)
{
	int rc;

	rc = kobject_synth_uevent(&drv->p->kobj, buf, count);
	return rc ? rc : count;
}
static DRIVER_ATTR_WO(uevent);

 
int bus_add_driver(struct device_driver *drv)
{
	struct bus_type *bus;
	struct driver_private *priv;
	int error = 0;

	bus = bus_get(drv->bus);
	if (!bus)
		return -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		error = -ENOMEM;
		goto out_put_bus;
	}
	klist_init(&priv->klist_devices, NULL, NULL);
	priv->driver = drv;
	drv->p = priv;
	priv->kobj.kset = bus->p->drivers_kset;
	error = kobject_init_and_add(&priv->kobj, &driver_ktype, NULL,
				     "%s", drv->name);
	if (error)
		goto out_unregister;

	klist_add_tail(&priv->knode_bus, &bus->p->klist_drivers);
	if (drv->bus->p->drivers_autoprobe) {
		error = driver_attach(drv);
		if (error)
			goto out_del_list;
	}
	module_add_driver(drv->owner, drv);

	error = driver_create_file(drv, &driver_attr_uevent);
	if (error) {
		printk(KERN_ERR "%s: uevent attr (%s) failed\n",
			__func__, drv->name);
	}
	error = driver_add_groups(drv, bus->drv_groups);
	if (error) {
		 
		printk(KERN_ERR "%s: driver_add_groups(%s) failed\n",
			__func__, drv->name);
	}

	if (!drv->suppress_bind_attrs) {
		error = add_bind_files(drv);
		if (error) {
			 
			printk(KERN_ERR "%s: add_bind_files(%s) failed\n",
				__func__, drv->name);
		}
	}

	return 0;

out_del_list:
	klist_del(&priv->knode_bus);
out_unregister:
	kobject_put(&priv->kobj);
	 
	drv->p = NULL;
out_put_bus:
	bus_put(bus);
	return error;
}

 
void bus_remove_driver(struct device_driver *drv)
{
	if (!drv->bus)
		return;

	if (!drv->suppress_bind_attrs)
		remove_bind_files(drv);
	driver_remove_groups(drv, drv->bus->drv_groups);
	driver_remove_file(drv, &driver_attr_uevent);
	klist_remove(&drv->p->knode_bus);
	driver_detach(drv);
	module_remove_driver(drv);
	kobject_put(&drv->p->kobj);
	bus_put(drv->bus);
}

/* Stubbed: bus_rescan_devices not used externally */
int bus_rescan_devices(struct bus_type *bus) { return 0; }

/* Stubbed: device_reprobe not used externally */
int device_reprobe(struct device *dev) { return 0; }

static int bus_add_groups(struct bus_type *bus,
			  const struct attribute_group **groups)
{
	return sysfs_create_groups(&bus->p->subsys.kobj, groups);
}

static void bus_remove_groups(struct bus_type *bus,
			      const struct attribute_group **groups)
{
	sysfs_remove_groups(&bus->p->subsys.kobj, groups);
}

static void klist_devices_get(struct klist_node *n)
{
	struct device_private *dev_prv = to_device_private_bus(n);
	struct device *dev = dev_prv->device;

	get_device(dev);
}

static void klist_devices_put(struct klist_node *n)
{
	struct device_private *dev_prv = to_device_private_bus(n);
	struct device *dev = dev_prv->device;

	put_device(dev);
}

static ssize_t bus_uevent_store(struct bus_type *bus,
				const char *buf, size_t count)
{
	int rc;

	rc = kobject_synth_uevent(&bus->p->subsys.kobj, buf, count);
	return rc ? rc : count;
}
 
static struct bus_attribute bus_attr_uevent = __ATTR(uevent, 0200, NULL,
						     bus_uevent_store);

 
int bus_register(struct bus_type *bus)
{
	int retval;
	struct subsys_private *priv;
	struct lock_class_key *key = &bus->lock_key;

	priv = kzalloc(sizeof(struct subsys_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus = bus;
	bus->p = priv;

	BLOCKING_INIT_NOTIFIER_HEAD(&priv->bus_notifier);

	retval = kobject_set_name(&priv->subsys.kobj, "%s", bus->name);
	if (retval)
		goto out;

	priv->subsys.kobj.kset = bus_kset;
	priv->subsys.kobj.ktype = &bus_ktype;
	priv->drivers_autoprobe = 1;

	retval = kset_register(&priv->subsys);
	if (retval)
		goto out;

	retval = bus_create_file(bus, &bus_attr_uevent);
	if (retval)
		goto bus_uevent_fail;

	priv->devices_kset = kset_create_and_add("devices", NULL,
						 &priv->subsys.kobj);
	if (!priv->devices_kset) {
		retval = -ENOMEM;
		goto bus_devices_fail;
	}

	priv->drivers_kset = kset_create_and_add("drivers", NULL,
						 &priv->subsys.kobj);
	if (!priv->drivers_kset) {
		retval = -ENOMEM;
		goto bus_drivers_fail;
	}

	INIT_LIST_HEAD(&priv->interfaces);
	__mutex_init(&priv->mutex, "subsys mutex", key);
	klist_init(&priv->klist_devices, klist_devices_get, klist_devices_put);
	klist_init(&priv->klist_drivers, NULL, NULL);

	retval = add_probe_files(bus);
	if (retval)
		goto bus_probe_files_fail;

	retval = bus_add_groups(bus, bus->bus_groups);
	if (retval)
		goto bus_groups_fail;

	return 0;

bus_groups_fail:
	remove_probe_files(bus);
bus_probe_files_fail:
	kset_unregister(bus->p->drivers_kset);
bus_drivers_fail:
	kset_unregister(bus->p->devices_kset);
bus_devices_fail:
	bus_remove_file(bus, &bus_attr_uevent);
bus_uevent_fail:
	kset_unregister(&bus->p->subsys);
out:
	kfree(bus->p);
	bus->p = NULL;
	return retval;
}

 
void bus_unregister(struct bus_type *bus)
{
	if (bus->dev_root)
		device_unregister(bus->dev_root);
	bus_remove_groups(bus, bus->bus_groups);
	remove_probe_files(bus);
	kset_unregister(bus->p->drivers_kset);
	kset_unregister(bus->p->devices_kset);
	bus_remove_file(bus, &bus_attr_uevent);
	kset_unregister(&bus->p->subsys);
}

int bus_register_notifier(struct bus_type *bus, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&bus->p->bus_notifier, nb);
}

/* Stub: bus_unregister_notifier not used externally */
int bus_unregister_notifier(struct bus_type *bus, struct notifier_block *nb)
{
	return 0;
}

/* Stub: bus_get_kset not used externally */
struct kset *bus_get_kset(struct bus_type *bus)
{
	return NULL;
}

struct klist *bus_get_device_klist(struct bus_type *bus)
{
	return &bus->p->klist_devices;
}

/* Stub: bus_sort_breadthfirst not used externally */
void bus_sort_breadthfirst(struct bus_type *bus,
			   int (*compare)(const struct device *a,
					  const struct device *b))
{
}

 
/* Stub: subsys_dev_iter_init not used externally */
void subsys_dev_iter_init(struct subsys_dev_iter *iter, struct bus_type *subsys,
			  struct device *start, const struct device_type *type)
{
}

 
/* Stub: subsys_dev_iter_next not used externally */
struct device *subsys_dev_iter_next(struct subsys_dev_iter *iter)
{
	return NULL;
}

/* Stub: subsys_dev_iter_exit not used externally */
void subsys_dev_iter_exit(struct subsys_dev_iter *iter)
{
}

/* Stub: subsys_interface_register not used externally */
int subsys_interface_register(struct subsys_interface *sif)
{
	return 0;
}

/* Stub: subsys_interface_unregister not used externally */
void subsys_interface_unregister(struct subsys_interface *sif)
{
}

static void system_root_device_release(struct device *dev)
{
	kfree(dev);
}

static int subsys_register(struct bus_type *subsys,
			   const struct attribute_group **groups,
			   struct kobject *parent_of_root)
{
	struct device *dev;
	int err;

	err = bus_register(subsys);
	if (err < 0)
		return err;

	dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		goto err_dev;
	}

	err = dev_set_name(dev, "%s", subsys->name);
	if (err < 0)
		goto err_name;

	dev->kobj.parent = parent_of_root;
	dev->groups = groups;
	dev->release = system_root_device_release;

	err = device_register(dev);
	if (err < 0)
		goto err_dev_reg;

	subsys->dev_root = dev;
	return 0;

err_dev_reg:
	put_device(dev);
	dev = NULL;
err_name:
	kfree(dev);
err_dev:
	bus_unregister(subsys);
	return err;
}

 
int subsys_system_register(struct bus_type *subsys,
			   const struct attribute_group **groups)
{
	return subsys_register(subsys, groups, &system_kset->kobj);
}

 
/* Stub: subsys_virtual_register not used externally */
int subsys_virtual_register(struct bus_type *subsys,
			    const struct attribute_group **groups)
{
	return 0;
}

int __init buses_init(void)
{
	bus_kset = kset_create_and_add("bus", &bus_uevent_ops, NULL);
	if (!bus_kset)
		return -ENOMEM;

	system_kset = kset_create_and_add("system", NULL, &devices_kset->kobj);
	if (!system_kset)
		return -ENOMEM;

	return 0;
}
