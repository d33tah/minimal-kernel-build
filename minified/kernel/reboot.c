
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

/* Stub: reboot functions not needed for Hello World */
void kernel_restart_prepare(char *cmd)
{
}
void migrate_to_reboot_cpu(void)
{
}
void kernel_restart(char *cmd)
{
	machine_restart(cmd);
}
void kernel_halt(void)
{
	machine_halt();
}
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
void do_kernel_power_off(void)
{
}
bool kernel_can_power_off(void)
{
	return false;
}
void kernel_power_off(void)
{
	machine_power_off();
}

DEFINE_MUTEX(system_transition_mutex);

/* Stub: reboot syscall not needed for Hello World */
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	return -ENOSYS;
}
