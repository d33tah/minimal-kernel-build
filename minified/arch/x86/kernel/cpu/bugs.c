#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/utsname.h>
extern void check_bugs(void);
#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/msr.h>
#include <asm/alternative.h>
#include <asm/nospec-branch.h>

u64 __ro_after_init x86_amd_ls_cfg_base;
u64 __ro_after_init x86_amd_ls_cfg_ssbd_mask;

void __init check_bugs(void)
{
	identify_boot_cpu();

	if (boot_cpu_data.x86 < 4)
		panic("Kernel requires i486+ for 'invlpg' and other features");

	init_utsname()->machine[1] =
		'0' + (boot_cpu_data.x86 > 6 ? 6 : boot_cpu_data.x86);

	alternatives_patched = 1;
}
