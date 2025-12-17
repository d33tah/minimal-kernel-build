/* Minimal includes for vdso32 setup */
#include <linux/init.h>
#include <asm/vdso.h>

#define VDSO_DEFAULT	1

unsigned int __read_mostly vdso32_enabled = VDSO_DEFAULT;

/* vdso32_setup and __setups removed (~5 LOC) */

int __init sysenter_setup(void)
{
	init_vdso_image(&vdso_image_32);

	return 0;
}

