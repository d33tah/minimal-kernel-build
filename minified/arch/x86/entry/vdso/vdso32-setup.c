/* Minimal includes for vdso32 setup */
#include <linux/init.h>
#include <asm/vdso.h>

int __init sysenter_setup(void)
{
	init_vdso_image(&vdso_image_32);

	return 0;
}

