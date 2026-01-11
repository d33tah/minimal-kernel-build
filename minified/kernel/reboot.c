#include <linux/kmsg_dump.h>
#include <linux/reboot.h>
#include <linux/syscalls.h>

void emergency_restart(void)
{
	kmsg_dump(KMSG_DUMP_EMERG);
	machine_emergency_restart();
}

/* kernel_restart_prepare, migrate_to_reboot_cpu removed - never called */
/* kernel_restart, kernel_halt, kernel_power_off removed - never called */

/* system_transition_mutex removed - never used */

/* Stub: reboot syscall not needed for Hello World */
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	return -ENOSYS;
}
