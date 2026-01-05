#ifndef _LINUX_REBOOT_H
#define _LINUX_REBOOT_H

#include <linux/notifier.h>

struct device;

enum reboot_mode {
	REBOOT_UNDEFINED = -1,
	REBOOT_COLD = 0,
};
extern enum reboot_mode reboot_mode;
extern enum reboot_mode panic_reboot_mode;

enum reboot_type {
	BOOT_ACPI	= 'a',
};
extern enum reboot_type reboot_type;

/* reboot_default, reboot_cpu removed - never used */

extern void machine_restart(char *cmd);
extern void machine_halt(void);
extern void machine_power_off(void);
/* machine_shutdown removed - never called */

/* migrate_to_reboot_cpu, do_kernel_power_off, kernel_restart_prepare removed - never called */
/* kernel_restart, kernel_halt, kernel_power_off removed - never called */

extern void emergency_restart(void);
extern void machine_emergency_restart(void);

#endif
