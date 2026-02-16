/* VDSO image stub - VDSO is disabled, but the symbol is still referenced */
#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/vdso.h>

const struct vdso_image vdso_image_32 = {};
