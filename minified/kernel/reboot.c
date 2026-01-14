#include <linux/kmsg_dump.h>
#include <linux/reboot.h>
#include <linux/syscalls.h>

void emergency_restart(void)
{
	kmsg_dump(KMSG_DUMP_EMERG);
	machine_emergency_restart();
}

/* Stub: reboot syscall not needed for Hello World */
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	return -ENOSYS;
}
