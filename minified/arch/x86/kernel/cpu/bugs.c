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

/* switch_to_cond_stibp, switch_mm_cond_ibpb, switch_mm_always_ibpb,
   mds_user_clear, mds_idle_clear definitions removed - never enabled */

void write_spec_ctrl_current(u64 val, bool force)
{
}

/* spec_ctrl_current removed - never called */

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

int arch_prctl_spec_ctrl_get(struct task_struct *task, unsigned long which)
{
	return -ENODEV;
}

int arch_prctl_spec_ctrl_set(struct task_struct *task, unsigned long which,
			     unsigned long ctrl)
{
	return -ENODEV;
}
/* switch_mm_cond_l1d_flush definition removed - never enabled */
