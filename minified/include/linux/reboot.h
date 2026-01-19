#ifndef _LINUX_REBOOT_H
#define _LINUX_REBOOT_H

#include <linux/notifier.h>

struct device;

/* enum reboot_mode, reboot_mode, panic_reboot_mode removed - only set, never read */

/* reboot_type enum and extern removed - never used */
/* machine_restart, machine_emergency_restart merged into kernel/panic.c - now static */
/* machine_halt, machine_power_off, machine_shutdown removed - never called */
/* migrate_to_reboot_cpu, do_kernel_power_off, kernel_restart_prepare removed - never called */
/* kernel_restart, kernel_halt, kernel_power_off removed - never called */

extern void emergency_restart(void);

#endif
