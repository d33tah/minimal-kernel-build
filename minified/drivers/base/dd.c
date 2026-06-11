
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/dma-map-ops.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/async.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "base.h"
#include "power/power.h"

static DEFINE_MUTEX(deferred_probe_mutex);
static LIST_HEAD(deferred_probe_pending_list);

/* Removed: deferred_probe_active_list, deferred_trigger_count, initcalls_done,
   deferred_probe_work[_func] + driver_deferred_probe_trigger. The trigger and
   work were only ever reached from driver_probe_device (now dead, no driver
   registers), so the active list was never populated and the work never ran. */

void driver_deferred_probe_add(struct device *dev)
{
	if (!dev->can_match)
		return;

	mutex_lock(&deferred_probe_mutex);
	if (list_empty(&dev->p->deferred_probe)) {
		dev_dbg(dev, "Added to deferred list\n");
		list_add_tail(&dev->p->deferred_probe, &deferred_probe_pending_list);
	}
	mutex_unlock(&deferred_probe_mutex);
}

void driver_deferred_probe_del(struct device *dev)
{
	mutex_lock(&deferred_probe_mutex);
	if (!list_empty(&dev->p->deferred_probe)) {
		dev_dbg(dev, "Removed from deferred list\n");
		list_del_init(&dev->p->deferred_probe);
	}
	mutex_unlock(&deferred_probe_mutex);
}

int driver_deferred_probe_timeout;



/* Removed: deferred_probe_timeout_work[_func] + deferred_probe_extend_timeout
   - only caller was driver_register, which is dead */

/* Stub: simplified deferred probe for minimal kernel */
static int deferred_probe_initcall(void)
{
	return 0;
}
late_initcall(deferred_probe_initcall);

/* Removed: device_is_bound, driver_bound, driver_sysfs_add/remove,
   device_bind_driver, device_unbind_cleanup, device_remove, call_driver_probe,
   really_probe, __driver_probe_device, driver_probe_device + probe_count/
   probe_waitqueue. No driver registers on any bus (driver_register/bus_add_driver
   are gone), so bus_for_each_drv()'s klist is always empty, the __device_attach
   callback never fires, and the entire probe/bind machinery below it is dead. */

struct device_attach_data {
	struct device *dev;
};

static int __device_attach_driver(struct device_driver *drv, void *_data)
{
	/* No driver ever registers on any bus, so bus_for_each_drv()'s klist is
	   always empty and this callback is never invoked. */
	return 0;
}

static int __device_attach(struct device *dev, bool allow_async)
{
	int ret = 0;

	device_lock(dev);
	if (!dev->p->dead) {
		struct device_attach_data data = {
			.dev = dev,
		};

		if (dev->parent)
			pm_runtime_get_sync(dev->parent);

		ret = bus_for_each_drv(dev->bus, NULL, &data,
					__device_attach_driver);
		pm_request_idle(dev);

		if (dev->parent)
			pm_runtime_put(dev->parent);
	}
	device_unlock(dev);
	return ret;
}

void device_initial_probe(struct device *dev)
{
	__device_attach(dev, true);
}

static void __device_driver_lock(struct device *dev, struct device *parent)
{
	if (parent && dev->bus->need_parent_lock)
		device_lock(parent);
	device_lock(dev);
}

static void __device_driver_unlock(struct device *dev, struct device *parent)
{
	device_unlock(dev);
	if (parent && dev->bus->need_parent_lock)
		device_unlock(parent);
}


/* Removed: __driver_attach_async_helper, __driver_attach, driver_attach -
   only reached via bus_add_driver, which is dead (no driver registers) */

static void __device_release_driver(struct device *dev)
{
	/* No driver ever binds in this minimal kernel (no driver registers,
	   so really_probe never runs and dev->driver stays NULL), making the
	   release path dead. */
}

void device_release_driver_internal(struct device *dev,
				    struct device_driver *drv,
				    struct device *parent)
{
	__device_driver_lock(dev, parent);

	if (!drv || drv == dev->driver)
		__device_release_driver(dev);

	__device_driver_unlock(dev, parent);
}

void device_release_driver(struct device *dev)
{
	 
	device_release_driver_internal(dev, NULL, NULL);
}


/* Removed: driver_detach - only reached via bus_remove_driver, which is dead */
