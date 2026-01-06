#include <linux/export.h>
#include <linux/reboot.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <asm/reboot.h>
#include <asm/io.h>

/* pm_power_off, port_cf9_safe, machine_real_restart removed - never called */

void machine_emergency_restart(void)
{
	machine_restart(NULL);
}

void machine_restart(char *cmd)
{
	while (1)
		halt();
}

void machine_halt(void)
{
	while (1)
		halt();
}

void run_crash_ipi_callback(struct pt_regs *regs)
{
}
