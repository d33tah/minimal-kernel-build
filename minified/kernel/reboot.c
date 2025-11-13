// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Minimal stub for reboot functionality
 *  Original: linux/kernel/reboot.c
 */

#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/reboot.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/pid_namespace.h>
#include <linux/capability.h>

/* Global variables required by external code */
int reboot_default = 1;
int reboot_cpu;
enum reboot_type reboot_type = BOOT_ACPI;
int reboot_force;
enum reboot_mode reboot_mode;
enum reboot_mode panic_reboot_mode = REBOOT_UNDEFINED;
struct pid *cad_pid;
void __weak (*pm_power_off)(void);

/* Notifier list for reboot events - defined in notifier.c */
extern struct blocking_notifier_head reboot_notifier_list;
static ATOMIC_NOTIFIER_HEAD(restart_handler_list);

/* Minimal emergency_restart - called from panic.c */
void emergency_restart(void)
{
	machine_emergency_restart();
}

/* Minimal kernel_restart */
void kernel_restart(char *cmd)
{
	machine_restart(cmd);
}

/* Minimal kernel_halt */
void kernel_halt(void)
{
	machine_halt();
}

/* Minimal kernel_power_off */
void kernel_power_off(void)
{
	machine_power_off();
}

/* Register/unregister reboot notifiers */
int register_reboot_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&reboot_notifier_list, nb);
}

int unregister_reboot_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&reboot_notifier_list, nb);
}

/* Devm variants */
static void devm_unregister_reboot_notifier(struct device *dev, void *res)
{
	unregister_reboot_notifier(*(struct notifier_block **)res);
}

int devm_register_reboot_notifier(struct device *dev, struct notifier_block *nb)
{
	struct notifier_block **rcnb;
	int ret;

	rcnb = devres_alloc(devm_unregister_reboot_notifier,
			    sizeof(*rcnb), GFP_KERNEL);
	if (!rcnb)
		return -ENOMEM;

	ret = register_reboot_notifier(nb);
	if (!ret) {
		*rcnb = nb;
		devres_add(dev, rcnb);
	} else {
		devres_free(rcnb);
	}

	return ret;
}

/* Restart handler registration */
int register_restart_handler(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&restart_handler_list, nb);
}

int unregister_restart_handler(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&restart_handler_list, nb);
}

void do_kernel_restart(char *cmd)
{
	atomic_notifier_call_chain(&restart_handler_list, reboot_mode, cmd);
}

/* Stub sys-off handler functions */
struct sys_off_handler {
	struct notifier_block nb;
	int (*sys_off_cb)(struct sys_off_data *data);
	void *cb_data;
	enum sys_off_mode mode;
	bool blocking;
	void *list;
};

static struct sys_off_handler platform_sys_off_handler;

struct sys_off_handler *
register_sys_off_handler(enum sys_off_mode mode,
			 int priority,
			 int (*callback)(struct sys_off_data *data),
			 void *cb_data)
{
	/* Minimal stub - just return a dummy handler */
	return &platform_sys_off_handler;
}

void unregister_sys_off_handler(struct sys_off_handler *handler)
{
	/* Minimal stub - do nothing */
}

static void devm_unregister_sys_off_handler(void *data)
{
	unregister_sys_off_handler(data);
}

int devm_register_sys_off_handler(struct device *dev,
				  enum sys_off_mode mode,
				  int priority,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data)
{
	struct sys_off_handler *handler;

	handler = register_sys_off_handler(mode, priority, callback, cb_data);
	if (IS_ERR(handler))
		return PTR_ERR(handler);

	return devm_add_action_or_reset(dev, devm_unregister_sys_off_handler,
					handler);
}

int devm_register_power_off_handler(struct device *dev,
				    int (*callback)(struct sys_off_data *data),
				    void *cb_data)
{
	return devm_register_sys_off_handler(dev,
					     SYS_OFF_MODE_POWER_OFF,
					     SYS_OFF_PRIO_DEFAULT,
					     callback, cb_data);
}

int devm_register_restart_handler(struct device *dev,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data)
{
	return devm_register_sys_off_handler(dev,
					     SYS_OFF_MODE_RESTART,
					     SYS_OFF_PRIO_DEFAULT,
					     callback, cb_data);
}

static struct sys_off_handler *platform_power_off_handler;

int register_platform_power_off(void (*power_off)(void))
{
	/* Minimal stub */
	return 0;
}

void unregister_platform_power_off(void (*power_off)(void))
{
	/* Minimal stub */
}

void do_kernel_power_off(void)
{
	/* Minimal stub */
}

bool kernel_can_power_off(void)
{
	return pm_power_off != NULL;
}

/* Minimal reboot syscall */
DEFINE_MUTEX(system_transition_mutex);

SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(current);

	if (!ns_capable(pid_ns->user_ns, CAP_SYS_BOOT))
		return -EPERM;

	if (magic1 != LINUX_REBOOT_MAGIC1 ||
	    (magic2 != LINUX_REBOOT_MAGIC2 &&
	     magic2 != LINUX_REBOOT_MAGIC2A &&
	     magic2 != LINUX_REBOOT_MAGIC2B &&
	     magic2 != LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	/* For minimal kernel, just return error */
	return -EINVAL;
}

/* Stubs for orderly poweroff/reboot */
void orderly_poweroff(bool force)
{
	/* Minimal stub */
}

void orderly_reboot(void)
{
	/* Minimal stub */
}

void hw_protection_shutdown(const char *reason, int ms_until_forced)
{
	/* Minimal stub */
}

/* Ctrl-alt-del handler */
void ctrl_alt_del(void)
{
	/* Minimal stub */
}

/* Command line setup */
static int __init reboot_setup(char *str)
{
	/* Minimal stub - just return success */
	return 1;
}
__setup("reboot=", reboot_setup);
