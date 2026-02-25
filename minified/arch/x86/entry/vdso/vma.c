/* VDSO disabled - only stubs needed for link */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/page_types.h>
/* vdso.h inlined */
struct vdso_image {};
extern bool fixup_vdso_exception(struct pt_regs *regs, int trapnr,
				 unsigned long error_code,
				 unsigned long fault_addr);

int __init sysenter_setup(void)
{
	return 0;
}
