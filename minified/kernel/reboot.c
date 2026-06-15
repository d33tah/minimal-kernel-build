
#define pr_fmt(fmt)	"reboot: " fmt

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

void __weak (*pm_power_off)(void);

void emergency_restart(void)
{
	kmsg_dump(KMSG_DUMP_EMERG);
	machine_emergency_restart();
}
