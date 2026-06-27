#include <linux/init.h>
#include <linux/utsname.h>
#include <asm/bugs.h>
#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/msr.h>
#include <asm/alternative.h>
#include <asm/nospec-branch.h>

void __init check_bugs(void)
{
	identify_boot_cpu();

	if (boot_cpu_data.x86 < 4)
		panic("Kernel requires i486+ for 'invlpg' and other features");

	init_utsname()->machine[1] =
		'0' + (boot_cpu_data.x86 > 6 ? 6 : boot_cpu_data.x86);
	
	alternative_instructions();
}

/* x86_virt_spec_ctrl, x86_spec_ctrl_setup_ap, itlb_multihit_kvm_mitigation,
 * l1tf_vmx_mitigation, vmx_l1d_flush_state removed - unused in minimal kernel */
