/* VDSO disabled - only stubs needed for link */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/vdso.h>

unsigned int vclocks_used __read_mostly;

const struct vdso_image vdso_image_32 = {};

int __init sysenter_setup(void)
{
	return 0;
}
