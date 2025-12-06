#ifndef _LINUX_REBOOT_H
#define _LINUX_REBOOT_H


#include <linux/notifier.h>

/* From uapi/linux/reboot.h - inlined */
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
extern int reboot_force;


extern int register_reboot_notifier(struct notifier_block *);
extern int unregister_reboot_notifier(struct notifier_block *);

extern int devm_register_reboot_notifier(struct device *, struct notifier_block *);

extern int register_restart_handler(struct notifier_block *);
extern int unregister_restart_handler(struct notifier_block *);
extern void do_kernel_restart(char *cmd);


extern void migrate_to_reboot_cpu(void);
extern void machine_restart(char *cmd);
extern void machine_halt(void);
extern void machine_power_off(void);

extern void machine_shutdown(void);
struct pt_regs;
extern void machine_crash_shutdown(struct pt_regs *);

void do_kernel_power_off(void);


#define SYS_OFF_PRIO_PLATFORM		-256
#define SYS_OFF_PRIO_LOW		-128
#define SYS_OFF_PRIO_DEFAULT		0
#define SYS_OFF_PRIO_HIGH		192
#define SYS_OFF_PRIO_FIRMWARE		224

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

int devm_register_sys_off_handler(struct device *dev,
				  enum sys_off_mode mode,
				  int priority,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data);

int devm_register_power_off_handler(struct device *dev,
				    int (*callback)(struct sys_off_data *data),
				    void *cb_data);

int devm_register_restart_handler(struct device *dev,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data);

int register_platform_power_off(void (*power_off)(void));
void unregister_platform_power_off(void (*power_off)(void));


extern void kernel_restart_prepare(char *cmd);
extern void kernel_restart(char *cmd);
extern void kernel_halt(void);
extern void kernel_power_off(void);
extern bool kernel_can_power_off(void);

void ctrl_alt_del(void);

extern void orderly_poweroff(bool force);
extern void orderly_reboot(void);
void hw_protection_shutdown(const char *reason, int ms_until_forced);


extern void emergency_restart(void);
#include <asm/emergency-restart.h>

#endif  
