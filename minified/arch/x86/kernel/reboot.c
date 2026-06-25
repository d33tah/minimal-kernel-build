#include <linux/export.h>
#include <linux/reboot.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <asm/reboot.h>
#include <asm/io.h>

void (*pm_power_off)(void);

void machine_emergency_restart(void)
{
	while (1)
		halt();
}
