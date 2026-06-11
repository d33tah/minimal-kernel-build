#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/utsname.h>
#include <asm/bugs.h>
#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/msr.h>
#include <asm/alternative.h>
#include <asm/nospec-branch.h>

u64 x86_spec_ctrl_base;

DEFINE_PER_CPU(u64, x86_spec_ctrl_current);

u64 __ro_after_init x86_amd_ls_cfg_base;
u64 __ro_after_init x86_amd_ls_cfg_ssbd_mask;

DEFINE_STATIC_KEY_FALSE(mds_user_clear);

DEFINE_STATIC_KEY_FALSE(mds_idle_clear);

void write_spec_ctrl_current(u64 val, bool force)
{
}

u64 spec_ctrl_current(void)
{
	return 0;
}

void __init check_bugs(void)
{
	identify_boot_cpu();

	if (boot_cpu_data.x86 < 4)
		panic("Kernel requires i486+ for 'invlpg' and other features");

	init_utsname()->machine[1] =
		'0' + (boot_cpu_data.x86 > 6 ? 6 : boot_cpu_data.x86);
	
	alternative_instructions();
	fpu__init_check_bugs();
}

/* x86_virt_spec_ctrl, x86_spec_ctrl_setup_ap, itlb_multihit_kvm_mitigation,
 * l1tf_vmx_mitigation, vmx_l1d_flush_state removed - unused in minimal kernel */
