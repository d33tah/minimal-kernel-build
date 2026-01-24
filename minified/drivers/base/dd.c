
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
static LIST_HEAD(deferred_probe_active_list);
static atomic_t deferred_trigger_count = ATOMIC_INIT(0);

static bool defer_all_probes;

static void __device_set_deferred_probe_reason(const struct device *dev,
					       char *reason)
{
	kfree(dev->p->deferred_probe_reason);
	dev->p->deferred_probe_reason = reason;
}

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

		__device_set_deferred_probe_reason(dev, NULL);

		mutex_unlock(&deferred_probe_mutex);

		device_pm_move_to_tail(dev);

		bus_probe_device(dev);
		mutex_lock(&deferred_probe_mutex);

		put_device(dev);
	}
	mutex_unlock(&deferred_probe_mutex);
}
static DECLARE_WORK(deferred_probe_work, deferred_probe_work_func);

void driver_deferred_probe_add(struct device *dev)
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

void driver_deferred_probe_del(struct device *dev)
{
	mutex_lock(&deferred_probe_mutex);
	if (!list_empty(&dev->p->deferred_probe)) {
		list_del_init(&dev->p->deferred_probe);
		__device_set_deferred_probe_reason(dev, NULL);
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

int driver_deferred_probe_timeout;

static void deferred_probe_timeout_work_func(struct work_struct *work)
{
	driver_deferred_probe_timeout = 0;
	driver_deferred_probe_trigger();
	/* flush_work removed - stub returning false */
}
static DECLARE_DELAYED_WORK(deferred_probe_timeout_work,
			    deferred_probe_timeout_work_func);

void deferred_probe_extend_timeout(void)
{
	if (cancel_delayed_work(&deferred_probe_timeout_work)) {
		schedule_delayed_work(&deferred_probe_timeout_work,
				      driver_deferred_probe_timeout * HZ);
	}
}

/* Stub: simplified deferred probe for minimal kernel */
static int deferred_probe_initcall(void)
{
	driver_deferred_probe_enable = true;
	return 0;
}
late_initcall(deferred_probe_initcall);

bool device_is_bound(struct device *dev)
{
	return dev->p && klist_node_attached(&dev->p->knode_driver);
}

static atomic_t probe_count = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(probe_waitqueue);

/* Stub: state_synced_show not needed for minimal kernel */

static void device_unbind_cleanup(struct device *dev)
{
	arch_teardown_dma_ops(dev);
	dev->driver = NULL;
	dev_set_drvdata(dev, NULL);
	if (dev->pm_domain && dev->pm_domain->dismiss)
		dev->pm_domain->dismiss(dev);
	dev_pm_set_driver_flags(dev, 0);
}

/* device_remove, call_driver_probe, really_probe inlined into single caller */

int driver_probe_done(void)
{
	int local_probe_count = atomic_read(&probe_count);

	if (local_probe_count)
		return -EBUSY;
	return 0;
}

void wait_for_device_probe(void)
{
	/* flush_work removed - stub returning false */
	wait_event(probe_waitqueue, atomic_read(&probe_count) == 0);
}

static int __driver_probe_device(struct device_driver *drv, struct device *dev)
{
	int ret = 0;

	if (dev->p->dead || !device_is_registered(dev))
		return -ENODEV;
	if (dev->driver)
		return -EBUSY;

	dev->can_match = true;

	if (dev->parent)
		pm_runtime_get_sync(dev->parent);

	/* Inlined really_probe */
	if (defer_all_probes) {
		ret = -EPROBE_DEFER;
	} else {
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

			if (dev->pm_domain && dev->pm_domain->activate) {
				ret = dev->pm_domain->activate(dev);
				if (ret)
					goto really_probe_failed;
			}

			if (dev->bus->probe)
				ret = dev->bus->probe(dev);
			else if (drv->probe)
				ret = drv->probe(dev);
			if (ret) {
				ret = -ret;
				goto really_probe_failed;
			}

			if (dev->pm_domain && dev->pm_domain->sync)
				dev->pm_domain->sync(dev);
			goto really_probe_done;

really_probe_failed:
			if (dev->bus && dev->bus->dma_cleanup)
				dev->bus->dma_cleanup(dev);
really_probe_pinctrl_failed:
			device_unbind_cleanup(dev);
really_probe_done:;
		}
	}
	pm_request_idle(dev);

	if (dev->parent)
		pm_runtime_put(dev->parent);

	return ret;
}

static int driver_probe_device(struct device_driver *drv, struct device *dev)
{
	int trigger_count = atomic_read(&deferred_trigger_count);
	int ret;

	atomic_inc(&probe_count);
	ret = __driver_probe_device(drv, dev);
	if (ret == -EPROBE_DEFER || ret == EPROBE_DEFER) {
		driver_deferred_probe_add(dev);

		if (trigger_count != atomic_read(&deferred_trigger_count) &&
		    !defer_all_probes)
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

	bool have_async;
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

static void __device_attach_async_helper(void *_dev, async_cookie_t cookie)
{
	struct device *dev = _dev;
	struct device_attach_data data = {
		.dev = dev,
		.check_async = true,
		.want_async = true,
	};

	device_lock(dev);

	if (dev->p->dead || dev->driver)
		goto out_unlock;

	if (dev->parent)
		pm_runtime_get_sync(dev->parent);

	bus_for_each_drv(dev->bus, NULL, &data, __device_attach_driver);

	pm_request_idle(dev);

	if (dev->parent)
		pm_runtime_put(dev->parent);
out_unlock:
	device_unlock(dev);

	put_device(dev);
}

static int __device_attach(struct device *dev, bool allow_async)
{
	int ret = 0;
	bool async = false;

	device_lock(dev);
	if (dev->p->dead) {
		goto out_unlock;
	} else if (dev->driver) {
		if (device_is_bound(dev)) {
			ret = 1;
			goto out_unlock;
		}
		/* device_bind_driver call removed - always returns 0 and does nothing */
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
		if (!ret && allow_async && data.have_async) {
			get_device(dev);
			async = true;
		} else {
			pm_request_idle(dev);
		}

		if (dev->parent)
			pm_runtime_put(dev->parent);
	}
out_unlock:
	device_unlock(dev);
	if (async)
		async_schedule_dev(__device_attach_async_helper, dev);
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

static int __driver_attach(struct device *dev, void *data)
{
	struct device_driver *drv = data;
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

	__device_driver_lock(dev, dev->parent);
	driver_probe_device(drv, dev);
	__device_driver_unlock(dev, dev->parent);

	return 0;
}

int driver_attach(struct device_driver *drv)
{
	return bus_for_each_dev(drv->bus, NULL, drv, __driver_attach);
}

static void __device_release_driver(struct device *dev, struct device *parent)
{
	struct device_driver *drv;

	drv = dev->driver;
	if (drv) {
		pm_runtime_get_sync(dev);
		/* device_links_busy always false - removed dead while loop */

		pm_runtime_put_sync(dev);

		if (dev->bus && dev->bus->remove)
			dev->bus->remove(dev);
		else if (dev->driver->remove)
			dev->driver->remove(dev);

		if (dev->bus && dev->bus->dma_cleanup)
			dev->bus->dma_cleanup(dev);

		device_unbind_cleanup(dev);

		klist_remove(&dev->p->knode_driver);

		kobject_uevent(&dev->kobj, KOBJ_UNBIND);
	}
}

void device_release_driver_internal(struct device *dev,
				    struct device_driver *drv,
				    struct device *parent)
{
	__device_driver_lock(dev, parent);

	if (!drv || drv == dev->driver)
		__device_release_driver(dev, parent);

	__device_driver_unlock(dev, parent);
}

void device_release_driver(struct device *dev)
{
	device_release_driver_internal(dev, NULL, NULL);
}

/* driver_detach removed - only called from bus_remove_driver which is never called (~22 LOC) */

/* Merged from kernel/async.c - stub async scheduler runs synchronously */
struct async_domain async_dfl_domain = { .pending = LIST_HEAD_INIT(
						 async_dfl_domain.pending),
					 .registered = 0 };
async_cookie_t async_schedule_node_domain(async_func_t func, void *data,
					  int node, struct async_domain *domain)
{
	func(data, 0);
	return 0;
}
