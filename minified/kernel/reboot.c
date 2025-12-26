
#define pr_fmt(fmt) "reboot: " fmt

#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/ctype.h>
#include <linux/export.h>
#include <linux/kexec.h>
#include <linux/kmod.h>
#include <linux/kmsg_dump.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/syscalls.h>
#include <linux/syscore_ops.h>
#include <linux/uaccess.h>

/* Removed: C_A_D, cad_pid - only set but never read */

#define DEFAULT_REBOOT_MODE
enum reboot_mode reboot_mode DEFAULT_REBOOT_MODE;
enum reboot_mode panic_reboot_mode = REBOOT_UNDEFINED;

int reboot_default = 1;
int reboot_cpu;
enum reboot_type reboot_type = BOOT_ACPI;

struct sys_off_handler {
	struct notifier_block nb;
	int (*sys_off_cb)(struct sys_off_data *data);
	void *cb_data;
	enum sys_off_mode mode;
	bool blocking;
	void *list;
};

void __weak (*pm_power_off)(void);

void emergency_restart(void)
{
	kmsg_dump(KMSG_DUMP_EMERG);
	machine_emergency_restart();
}

void kernel_restart_prepare(char *cmd)
{
	blocking_notifier_call_chain(&reboot_notifier_list, SYS_RESTART, cmd);
	system_state = SYSTEM_RESTART;
	usermodehelper_disable();
	device_shutdown();
}

/* Reboot notifier functions removed - unused:
 * register_reboot_notifier, unregister_reboot_notifier, devm_register_reboot_notifier,
 * register_restart_handler, unregister_restart_handler, do_kernel_restart */

void migrate_to_reboot_cpu(void)
{
	int cpu = reboot_cpu;

	cpu_hotplug_disable();

	if (!cpu_online(cpu))
		cpu = cpumask_first(cpu_online_mask);

	current->flags |= PF_NO_SETAFFINITY;

	set_cpus_allowed_ptr(current, cpumask_of(cpu));
}

void kernel_restart(char *cmd)
{
	kernel_restart_prepare(cmd);
	migrate_to_reboot_cpu();
	syscore_shutdown();
	if (!cmd)
		pr_emerg("Restarting system\n");
	else
		pr_emerg("Restarting system with command '%s'\n", cmd);
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_restart(cmd);
}

static void kernel_shutdown_prepare(enum system_states state)
{
	blocking_notifier_call_chain(
		&reboot_notifier_list,
		(state == SYSTEM_HALT) ? SYS_HALT : SYS_POWER_OFF, NULL);
	system_state = state;
	usermodehelper_disable();
	device_shutdown();
}
void kernel_halt(void)
{
	kernel_shutdown_prepare(SYSTEM_HALT);
	migrate_to_reboot_cpu();
	syscore_shutdown();
	pr_emerg("System halted\n");
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_halt();
}

static BLOCKING_NOTIFIER_HEAD(power_off_prep_handler_list);

static ATOMIC_NOTIFIER_HEAD(power_off_handler_list);

/* Stubbed sys_off_handler infrastructure - not used externally */
struct sys_off_handler *
register_sys_off_handler(enum sys_off_mode mode, int priority,
			 int (*callback)(struct sys_off_data *data),
			 void *cb_data)
{
	return NULL;
}

void unregister_sys_off_handler(struct sys_off_handler *handler)
{
}

static int legacy_pm_power_off(struct sys_off_data *data)
{
	if (pm_power_off)
		pm_power_off();

	return NOTIFY_DONE;
}

static void do_kernel_power_off_prepare(void)
{
	blocking_notifier_call_chain(&power_off_prep_handler_list, 0, NULL);
}

void do_kernel_power_off(void)
{
	struct sys_off_handler *sys_off = NULL;

	if (pm_power_off)
		sys_off = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF,
						   SYS_OFF_PRIO_DEFAULT,
						   legacy_pm_power_off, NULL);

	atomic_notifier_call_chain(&power_off_handler_list, 0, NULL);

	unregister_sys_off_handler(sys_off);
}

bool kernel_can_power_off(void)
{
	return !atomic_notifier_call_chain_is_empty(&power_off_handler_list) ||
	       pm_power_off;
}

void kernel_power_off(void)
{
	kernel_shutdown_prepare(SYSTEM_POWER_OFF);
	do_kernel_power_off_prepare();
	migrate_to_reboot_cpu();
	syscore_shutdown();
	pr_emerg("Power down\n");
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_power_off();
}

DEFINE_MUTEX(system_transition_mutex);

/* Stub: reboot syscall not needed for Hello World */
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	return -ENOSYS;
}
