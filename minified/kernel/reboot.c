
#define pr_fmt(fmt)	"reboot: " fmt

#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/ctype.h>
#include <linux/export.h>
#include <linux/kmsg_dump.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

/* Removed: C_A_D, cad_pid - only set but never read */
/* Removed: pm_power_off - never assigned and never invoked on this build */

void emergency_restart(void)
{
	kmsg_dump(KMSG_DUMP_EMERG);
	machine_emergency_restart();
}
