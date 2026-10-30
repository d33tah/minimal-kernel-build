#include <linux/mm.h>
#include <asm/vdso.h>

void __init init_vdso_image(const struct vdso_image *image)
{
	BUG_ON(image->size % PAGE_SIZE != 0);
}

struct linux_binprm;

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	return 0;
}
