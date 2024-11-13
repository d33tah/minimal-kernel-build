// SPDX-License-Identifier: GPL-2.0
#include <linux/tboot.h>

#include <asm/cpufeature.h>
#include <asm/msr-index.h>
#include <asm/processor.h>
#include <asm/vmx.h>
#include "cpu.h"

#undef pr_fmt
#define pr_fmt(fmt)	"x86/cpu: " fmt


static int __init nosgx(char *str)
{
	setup_clear_cpu_cap(X86_FEATURE_SGX);

	return 0;
}

early_param("nosgx", nosgx);

void init_ia32_feat_ctl(struct cpuinfo_x86 *c)
{
	bool enable_sgx_kvm = false, enable_sgx_driver = false;
	bool tboot = tboot_enabled();
	bool enable_vmx;
	u64 msr;

	if (rdmsrl_safe(MSR_IA32_FEAT_CTL, &msr)) {
		clear_cpu_cap(c, X86_FEATURE_VMX);
		clear_cpu_cap(c, X86_FEATURE_SGX);
		return;
	}

	enable_vmx = cpu_has(c, X86_FEATURE_VMX) &&
		     IS_ENABLED(CONFIG_KVM_INTEL);

	if (cpu_has(c, X86_FEATURE_SGX) && IS_ENABLED(CONFIG_X86_SGX)) {
		/*
		 * Separate out SGX driver enabling from KVM.  This allows KVM
		 * guests to use SGX even if the kernel SGX driver refuses to
		 * use it.  This happens if flexible Launch Control is not
		 * available.
		 */
		enable_sgx_driver = cpu_has(c, X86_FEATURE_SGX_LC);
		enable_sgx_kvm = enable_vmx && IS_ENABLED(CONFIG_X86_SGX_KVM);
	}

	if (msr & FEAT_CTL_LOCKED)
		goto update_caps;

	/*
	 * Ignore whatever value BIOS left in the MSR to avoid enabling random
	 * features or faulting on the WRMSR.
	 */
	msr = FEAT_CTL_LOCKED;

	/*
	 * Enable VMX if and only if the kernel may do VMXON at some point,
	 * i.e. KVM is enabled, to avoid unnecessarily adding an attack vector
	 * for the kernel, e.g. using VMX to hide malicious code.
	 */
	if (enable_vmx) {
		msr |= FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX;

		if (tboot)
			msr |= FEAT_CTL_VMX_ENABLED_INSIDE_SMX;
	}

	if (enable_sgx_kvm || enable_sgx_driver) {
		msr |= FEAT_CTL_SGX_ENABLED;
		if (enable_sgx_driver)
			msr |= FEAT_CTL_SGX_LC_ENABLED;
	}

	wrmsrl(MSR_IA32_FEAT_CTL, msr);

update_caps:
	set_cpu_cap(c, X86_FEATURE_MSR_IA32_FEAT_CTL);

	if (!cpu_has(c, X86_FEATURE_VMX))
		goto update_sgx;

	if ( (tboot && !(msr & FEAT_CTL_VMX_ENABLED_INSIDE_SMX)) ||
	    (!tboot && !(msr & FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX))) {
		if (IS_ENABLED(CONFIG_KVM_INTEL))
			pr_err_once("VMX (%s TXT) disabled by BIOS\n",
				    tboot ? "inside" : "outside");
		clear_cpu_cap(c, X86_FEATURE_VMX);
	} else {
	}

update_sgx:
	if (!(msr & FEAT_CTL_SGX_ENABLED)) {
		if (enable_sgx_kvm || enable_sgx_driver)
			pr_err_once("SGX disabled by BIOS.\n");
		clear_cpu_cap(c, X86_FEATURE_SGX);
		return;
	}

	/*
	 * VMX feature bit may be cleared due to being disabled in BIOS,
	 * in which case SGX virtualization cannot be supported either.
	 */
	if (!cpu_has(c, X86_FEATURE_VMX) && enable_sgx_kvm) {
		pr_err_once("SGX virtualization disabled due to lack of VMX.\n");
		enable_sgx_kvm = 0;
	}

	if (!(msr & FEAT_CTL_SGX_LC_ENABLED) && enable_sgx_driver) {
		if (!enable_sgx_kvm) {
			pr_err_once("SGX Launch Control is locked. Disable SGX.\n");
			clear_cpu_cap(c, X86_FEATURE_SGX);
		} else {
			pr_err_once("SGX Launch Control is locked. Support SGX virtualization only.\n");
			clear_cpu_cap(c, X86_FEATURE_SGX_LC);
		}
	}
}
