/* Minimal includes for vdso32 setup */
#include <linux/init.h>
#include <asm/vdso.h>

int __init sysenter_setup(void)
{
	return 0;
}
