#include <linux/export.h>
#include <linux/reboot.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <asm/reboot.h>
#include <asm/io.h>

void (*pm_power_off)(void);

void machine_emergency_restart(void)
{
	machine_restart(NULL);
}

void machine_restart(char *cmd)
{
	while (1)
		halt();
}


void run_crash_ipi_callback(struct pt_regs *regs)
{
}
