// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub - reboot/shutdown not needed for minimal kernel
 */
#include <linux/export.h>
#include <linux/reboot.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <asm/reboot.h>
#include <asm/io.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

bool port_cf9_safe = false;

void __noreturn machine_real_restart(unsigned int type)
{
	while (1)
		halt();
}

void __attribute__((weak)) mach_reboot_fixups(void)
{
}

void native_machine_shutdown(void)
{
}

void machine_shutdown(void)
{
	native_machine_shutdown();
}

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

void machine_power_off(void)
{
	machine_halt();
}

void nmi_shootdown_cpus(nmi_shootdown_cb callback)
{
}

void run_crash_ipi_callback(struct pt_regs *regs)
{
}
