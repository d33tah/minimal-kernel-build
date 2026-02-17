
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#define RPM_ASYNC 0x01
#define RPM_GET_PUT 0x04
static inline int __pm_runtime_idle(struct device *dev, int rpmflags)
{
	return -ENOSYS;
}
static inline int __pm_runtime_resume(struct device *dev, int rpmflags)
{
	return 1;
}
static inline int pm_request_idle(struct device *dev)
{
	return __pm_runtime_idle(dev, RPM_ASYNC);
}
static inline int pm_runtime_get_sync(struct device *dev)
{
	return __pm_runtime_resume(dev, RPM_GET_PUT);
}
static inline int pm_runtime_put(struct device *dev)
{
	return __pm_runtime_idle(dev, RPM_GET_PUT | RPM_ASYNC);
}

#include <linux/slab.h>

#include "base.h"

static DEFINE_MUTEX(deferred_probe_mutex);
static LIST_HEAD(deferred_probe_pending_list);
static LIST_HEAD(deferred_probe_active_list);
static atomic_t deferred_trigger_count = ATOMIC_INIT(0);

static void deferred_probe_work_func(struct work_struct *work)
{
	struct device *dev;
	struct device_private *private;

	mutex_lock(&deferred_probe_mutex);
	while (!list_empty(&deferred_probe_active_list)) {
		private = list_first_entry(&deferred_probe_active_list,
					   typeof(*dev->p), deferred_probe);
		dev = private->device;
		list_del_init(&private->deferred_probe);

		get_device(dev);
		mutex_unlock(&deferred_probe_mutex);
		bus_probe_device(dev);
		mutex_lock(&deferred_probe_mutex);
		put_device(dev);
	}
	mutex_unlock(&deferred_probe_mutex);
}
static DECLARE_WORK(deferred_probe_work, deferred_probe_work_func);

static void driver_deferred_probe_add(struct device *dev)
{
	if (!dev->can_match)
		return;

	mutex_lock(&deferred_probe_mutex);
	if (list_empty(&dev->p->deferred_probe)) {
		list_add_tail(&dev->p->deferred_probe,
			      &deferred_probe_pending_list);
	}
	mutex_unlock(&deferred_probe_mutex);
}

static bool driver_deferred_probe_enable;
static void driver_deferred_probe_trigger(void)
{
	if (!driver_deferred_probe_enable)
		return;

	mutex_lock(&deferred_probe_mutex);
	atomic_inc(&deferred_trigger_count);
	list_splice_tail_init(&deferred_probe_pending_list,
			      &deferred_probe_active_list);
	mutex_unlock(&deferred_probe_mutex);

	queue_work(system_unbound_wq, &deferred_probe_work);
}

static int deferred_probe_initcall(void)
{
	driver_deferred_probe_enable = true;
	return 0;
}
late_initcall(deferred_probe_initcall);

static bool device_is_bound(struct device *dev)
{
	return dev->p && klist_node_attached(&dev->p->knode_driver);
}

static atomic_t probe_count = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(probe_waitqueue);

static void device_unbind_cleanup(struct device *dev)
{
	dev->driver = NULL;
	dev_set_drvdata(dev, NULL);
	dev->power.driver_flags = 0; /* dev_pm_set_driver_flags inlined */
}

void wait_for_device_probe(void)
{
	wait_event(probe_waitqueue, atomic_read(&probe_count) == 0);
}

static int driver_probe_device(struct device_driver *drv, struct device *dev)
{
	int trigger_count = atomic_read(&deferred_trigger_count);
	int ret = 0;

	atomic_inc(&probe_count);

	if (dev->p->dead ||
	    !dev->kobj.state_in_sysfs) { /* device_is_registered inlined */
		ret = -ENODEV;
		goto out;
	}
	if (dev->driver) {
		ret = -EBUSY;
		goto out;
	}

	dev->can_match = true;

	if (dev->parent)
		pm_runtime_get_sync(dev->parent);

	dev->links.status = DL_DEV_PROBING;

	if (!list_empty(&dev->devres_head)) {
		ret = -EBUSY;
	} else {
		dev->driver = drv;

		if (dev->bus->dma_configure) {
			ret = dev->bus->dma_configure(dev);
			if (ret)
				goto really_probe_pinctrl_failed;
		}

		if (dev->bus->probe)
			ret = dev->bus->probe(dev);
		else if (drv->probe)
			ret = drv->probe(dev);
		if (ret) {
			ret = -ret;
			goto really_probe_failed;
		}

		goto really_probe_done;

really_probe_failed:
		if (dev->bus && dev->bus->dma_cleanup)
			dev->bus->dma_cleanup(dev);
really_probe_pinctrl_failed:
		device_unbind_cleanup(dev);
really_probe_done:;
	}
	pm_request_idle(dev);

	if (dev->parent)
		pm_runtime_put(dev->parent);

out:
	if (ret == -EPROBE_DEFER || ret == EPROBE_DEFER) {
		driver_deferred_probe_add(dev);

		if (trigger_count != atomic_read(&deferred_trigger_count))
			driver_deferred_probe_trigger();
	}
	atomic_dec(&probe_count);
	wake_up_all(&probe_waitqueue);
	return ret;
}

struct device_attach_data {
	struct device *dev;
	bool check_async;
	bool want_async;
};

static int __device_attach_driver(struct device_driver *drv, void *_data)
{
	struct device_attach_data *data = _data;
	struct device *dev = data->dev;
	int ret;

	ret = driver_match_device(drv, dev);
	if (ret == 0) {
		return 0;
	} else if (ret == -EPROBE_DEFER) {
		dev->can_match = true;
		driver_deferred_probe_add(dev);
	} else if (ret < 0) {
		return ret;
	}

	if (data->check_async && data->want_async)
		return 0;

	ret = driver_probe_device(drv, dev);
	if (ret < 0)
		return ret;
	return ret == 0;
}

static int __device_attach(struct device *dev, bool allow_async)
{
	int ret = 0;

	device_lock(dev);
	if (dev->p->dead) {
		goto out_unlock;
	} else if (dev->driver) {
		if (device_is_bound(dev)) {
			ret = 1;
			goto out_unlock;
		}
		ret = 1;
	} else {
		struct device_attach_data data = {
			.dev = dev,
			.check_async = allow_async,
			.want_async = false,
		};

		if (dev->parent)
			pm_runtime_get_sync(dev->parent);

		ret = bus_for_each_drv(dev->bus, NULL, &data,
				       __device_attach_driver);
		pm_request_idle(dev);

		if (dev->parent)
			pm_runtime_put(dev->parent);
	}
out_unlock:
	device_unlock(dev);
	return ret;
}

void device_initial_probe(struct device *dev)
{
	__device_attach(dev, true);
}
