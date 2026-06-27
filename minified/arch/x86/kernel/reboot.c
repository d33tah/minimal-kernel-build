#include <linux/export.h>
#include <linux/reboot.h>
#include <linux/kernel.h>
#include <asm/io.h>

void machine_emergency_restart(void)
{
	while (1)
		halt();
}
