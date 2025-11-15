 
 

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/mm_types.h>
#include <linux/elf.h>

#include <asm/processor.h>
#include <asm/vdso.h>

#define VDSO_DEFAULT	1

 
unsigned int __read_mostly vdso32_enabled = VDSO_DEFAULT;

static int __init vdso32_setup(char *s)
{
	vdso32_enabled = simple_strtoul(s, NULL, 0);

	if (vdso32_enabled > 1) {
		pr_warn("vdso32 values other than 0 and 1 are no longer allowed; vdso disabled\n");
		vdso32_enabled = 0;
	}

	return 1;
}

 
__setup("vdso32=", vdso32_setup);

__setup_param("vdso=", vdso_setup, vdso32_setup, 0);

int __init sysenter_setup(void)
{
	init_vdso_image(&vdso_image_32);

	return 0;
}

