/* VDSO disabled - only stubs needed for link */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/vdso.h>

int __init sysenter_setup(void)
{
	return 0;
}
