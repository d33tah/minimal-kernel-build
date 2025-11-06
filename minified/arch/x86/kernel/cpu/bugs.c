// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub - CPU vulnerability mitigations not needed
 */
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/utsname.h>
#include <asm/bugs.h>
#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/msr.h>
#include <asm/alternative.h>
#include <asm/vmx.h>
#include <asm/nospec-branch.h>

/* Exported symbols that are referenced by other code */
u64 x86_spec_ctrl_base;
EXPORT_SYMBOL_GPL(x86_spec_ctrl_base);

DEFINE_PER_CPU(u64, x86_spec_ctrl_current);
EXPORT_SYMBOL_GPL(x86_spec_ctrl_current);

u64 __ro_after_init x86_amd_ls_cfg_base;
u64 __ro_after_init x86_amd_ls_cfg_ssbd_mask;

DEFINE_STATIC_KEY_FALSE(switch_to_cond_stibp);
DEFINE_STATIC_KEY_FALSE(switch_mm_cond_ibpb);
DEFINE_STATIC_KEY_FALSE(switch_mm_always_ibpb);

DEFINE_STATIC_KEY_FALSE(mds_user_clear);
EXPORT_SYMBOL_GPL(mds_user_clear);

DEFINE_STATIC_KEY_FALSE(mds_idle_clear);
EXPORT_SYMBOL_GPL(mds_idle_clear);

DEFINE_STATIC_KEY_FALSE(mmio_stale_data_clear);
EXPORT_SYMBOL_GPL(mmio_stale_data_clear);

void write_spec_ctrl_current(u64 val, bool force)
{
}

u64 spec_ctrl_current(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(spec_ctrl_current);

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

void x86_virt_spec_ctrl(u64 guest_spec_ctrl, u64 guest_virt_spec_ctrl, bool setguest)
{
}
EXPORT_SYMBOL_GPL(x86_virt_spec_ctrl);

void x86_spec_ctrl_setup_ap(void)
{
}

bool itlb_multihit_kvm_mitigation;
EXPORT_SYMBOL_GPL(itlb_multihit_kvm_mitigation);

enum l1tf_mitigations l1tf_mitigation = L1TF_MITIGATION_OFF;
EXPORT_SYMBOL_GPL(l1tf_mitigation);

enum vmx_l1d_flush_state l1tf_vmx_mitigation = VMENTER_L1D_FLUSH_AUTO;
EXPORT_SYMBOL_GPL(l1tf_vmx_mitigation);

int arch_prctl_spec_ctrl_get(struct task_struct *task, unsigned long which)
{
	return -ENODEV;
}

int arch_prctl_spec_ctrl_set(struct task_struct *task, unsigned long which,
			      unsigned long ctrl)
{
	return -ENODEV;
}

void update_srbds_msr(void)
{
}

void cpu_bugs_smt_update(void)
{
}

DEFINE_STATIC_KEY_FALSE(switch_mm_cond_l1d_flush);
