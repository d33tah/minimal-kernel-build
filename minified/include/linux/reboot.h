#ifndef _LINUX_REBOOT_H
#define _LINUX_REBOOT_H


#include <linux/notifier.h>

#define	LINUX_REBOOT_MAGIC1	0xfee1dead
#define	LINUX_REBOOT_MAGIC2	672274793
#define	LINUX_REBOOT_MAGIC2A	85072278
#define	LINUX_REBOOT_MAGIC2B	369367448
#define	LINUX_REBOOT_MAGIC2C	537993216
#define	LINUX_REBOOT_CMD_RESTART	0x01234567
#define	LINUX_REBOOT_CMD_HALT		0xCDEF0123
#define	LINUX_REBOOT_CMD_CAD_ON		0x89ABCDEF
#define	LINUX_REBOOT_CMD_CAD_OFF	0x00000000
#define	LINUX_REBOOT_CMD_POWER_OFF	0x4321FEDC
#define	LINUX_REBOOT_CMD_RESTART2	0xA1B2C3D4
#define	LINUX_REBOOT_CMD_SW_SUSPEND	0xD000FCE2
#define	LINUX_REBOOT_CMD_KEXEC		0x45584543

struct device;
struct sys_off_handler;

#define SYS_DOWN	0x0001	 
#define SYS_RESTART	SYS_DOWN
#define SYS_HALT	0x0002	 
#define SYS_POWER_OFF	0x0003	 

enum reboot_mode {
	REBOOT_UNDEFINED = -1,
	REBOOT_COLD = 0,
	REBOOT_WARM,
	REBOOT_HARD,
	REBOOT_SOFT,
	REBOOT_GPIO,
};
extern enum reboot_mode reboot_mode;
extern enum reboot_mode panic_reboot_mode;

enum reboot_type {
	BOOT_TRIPLE	= 't',
	BOOT_KBD	= 'k',
	BOOT_BIOS	= 'b',
	BOOT_ACPI	= 'a',
	BOOT_EFI	= 'e',
	BOOT_CF9_FORCE	= 'p',
	BOOT_CF9_SAFE	= 'q',
};
extern enum reboot_type reboot_type;

extern int reboot_default;
extern int reboot_cpu;

/* register_reboot_notifier, unregister_reboot_notifier, devm_register_reboot_notifier,
 * register_restart_handler, unregister_restart_handler, do_kernel_restart removed - unused */


extern void machine_restart(char *cmd);
extern void machine_halt(void);
extern void machine_power_off(void);

extern void machine_shutdown(void);
struct pt_regs;

extern void emergency_restart(void);

/* Inlined from asm/emergency-restart.h */
extern void machine_emergency_restart(void);

#endif  
