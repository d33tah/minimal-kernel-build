
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

/* Removed: deferred_probe_active_list, deferred_trigger_count, initcalls_done,
   deferred_probe_work[_func] + driver_deferred_probe_trigger. The trigger and
   work were only ever reached from driver_probe_device (now dead, no driver
   registers), so the active list was never populated and the work never ran. */

/* Removed: driver_deferred_probe_add - 0 callers. The active list was only
   populated from driver_probe_device (dead, no driver registers), so nothing
   is ever added to the deferred list. */

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

/* Removed: __device_attach_driver, __device_attach, device_initial_probe,
   __device_driver_lock/unlock, __device_release_driver,
   device_release_driver_internal, device_release_driver, driver_detach.
   No driver ever registers on any bus (driver_register/bus_add_driver are gone)
   so bus_for_each_drv()'s klist is always empty and no device ever binds a
   driver (dev->driver stays NULL). The attach side iterated an empty klist
   with a no-op callback and only touched fully-stubbed PM-runtime helpers; the
   release side had an empty body. Both chains are now unreachable from the live
   bus_probe_device/bus_remove_device device paths, so they are removed. */
