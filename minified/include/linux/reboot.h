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

extern int reboot_default;
extern int reboot_cpu;

extern void migrate_to_reboot_cpu(void);
extern void machine_restart(char *cmd);
extern void machine_halt(void);
extern void machine_power_off(void);
extern void machine_shutdown(void);

void do_kernel_power_off(void);

extern void kernel_restart_prepare(char *cmd);
extern void kernel_restart(char *cmd);
extern void kernel_halt(void);
extern void kernel_power_off(void);
extern bool kernel_can_power_off(void);

extern void emergency_restart(void);
extern void machine_emergency_restart(void);

#endif
