#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/utsname.h>
/* bugs.h inlined */
extern void check_bugs(void);
#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/msr.h>
#include <asm/alternative.h>
#include <asm/nospec-branch.h>

/* x86_spec_ctrl_base, x86_spec_ctrl_current removed - never used */

u64 __ro_after_init x86_amd_ls_cfg_base;
u64 __ro_after_init x86_amd_ls_cfg_ssbd_mask;

/* switch_to_cond_stibp, switch_mm_cond_ibpb, switch_mm_always_ibpb,
   mds_user_clear, mds_idle_clear, write_spec_ctrl_current, spec_ctrl_current
   removed - never enabled/never called */

void __init check_bugs(void)
{
	identify_boot_cpu();

	if (boot_cpu_data.x86 < 4)
		panic("Kernel requires i486+ for 'invlpg' and other features");

	init_utsname()->machine[1] =
		'0' + (boot_cpu_data.x86 > 6 ? 6 : boot_cpu_data.x86);

	/* alternative_instructions inlined - only sets alternatives_patched = 1 */
	alternatives_patched = 1;
}

/* x86_virt_spec_ctrl, x86_spec_ctrl_setup_ap, itlb_multihit_kvm_mitigation,
 * l1tf_vmx_mitigation, vmx_l1d_flush_state, arch_prctl_spec_ctrl_get,
 * arch_prctl_spec_ctrl_set, switch_mm_cond_l1d_flush removed - unused */
