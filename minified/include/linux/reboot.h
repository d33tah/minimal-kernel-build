#ifndef _LINUX_REBOOT_H
#define _LINUX_REBOOT_H

#include <linux/notifier.h>

struct device;
struct sys_off_handler;

enum reboot_mode {
	REBOOT_UNDEFINED = -1,
	REBOOT_COLD = 0,
};
extern enum reboot_mode reboot_mode;
extern enum reboot_mode panic_reboot_mode;

enum reboot_type {
	BOOT_ACPI	= 'a',
	BOOT_EFI	= 'e',
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

enum sys_off_mode {
	SYS_OFF_MODE_POWER_OFF_PREPARE,
	SYS_OFF_MODE_POWER_OFF,
	SYS_OFF_MODE_RESTART,
};

struct sys_off_data {
	int mode;
	void *cb_data;
	const char *cmd;
};

struct sys_off_handler *
register_sys_off_handler(enum sys_off_mode mode,
			 int priority,
			 int (*callback)(struct sys_off_data *data),
			 void *cb_data);
void unregister_sys_off_handler(struct sys_off_handler *handler);

extern void kernel_restart_prepare(char *cmd);
extern void kernel_restart(char *cmd);
extern void kernel_halt(void);
extern void kernel_power_off(void);
extern bool kernel_can_power_off(void);

extern void emergency_restart(void);
extern void machine_emergency_restart(void);

#endif
