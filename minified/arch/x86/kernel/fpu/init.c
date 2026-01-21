#include <asm/fpu/api.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/init.h>

#include "internal.h"
#include "legacy.h"
#include "xstate.h"

/* fpu__init_cpu_generic inlined - single caller */
void fpu__init_cpu(void)
{
	unsigned long cr0;
	unsigned long cr4_mask = 0;

	if (boot_cpu_has(X86_FEATURE_FXSR))
		cr4_mask |= X86_CR4_OSFXSR;
	if (boot_cpu_has(X86_FEATURE_XMM))
		cr4_mask |= X86_CR4_OSXMMEXCPT;
	if (cr4_mask)
		cr4_set_bits(cr4_mask);

	cr0 = read_cr0();
	cr0 &= ~(X86_CR0_TS | X86_CR0_EM);
	if (!boot_cpu_has(X86_FEATURE_FPU))
		cr0 |= X86_CR0_EM;
	write_cr0(cr0);

	asm volatile("fninit");
}

/* fpu__probe_without_cpuid removed - never called */

static void fpu__init_system_early_generic(struct cpuinfo_x86 *c)
{
	/* Stubbed - force FPU capability for minimal boot */
	setup_force_cpu_cap(X86_FEATURE_FPU);
}

unsigned int mxcsr_feature_mask __ro_after_init = 0xffffffffu;

/* fpu__init_system_mxcsr, fpu__init_task_struct_size,
 * fpu__init_system_xstate_size_legacy all inlined below */

#define TYPE_ALIGN(TYPE)           \
	offsetof(                  \
		struct {           \
			char x;    \
			TYPE test; \
		},                 \
		test)

#define CHECK_MEMBER_AT_END_OF(TYPE, MEMBER) \
	BUILD_BUG_ON(sizeof(TYPE) !=         \
		     ALIGN(offsetofend(TYPE, MEMBER), TYPE_ALIGN(TYPE)))

void __init fpu__init_system(struct cpuinfo_x86 *c)
{
	unsigned int mask = 0;
	unsigned int size;
	int task_size;

	fpstate_reset(&current->thread.fpu);
	fpu__init_system_early_generic(c);

	fpu__init_cpu();

	fpstate_init_user(&init_fpstate);

	/* fpu__init_system_mxcsr inlined */
	if (boot_cpu_has(X86_FEATURE_FXSR)) {
		static struct fxregs_state fxregs __initdata;
		asm volatile("fxsave %0" : "+m"(fxregs));
		mask = fxregs.mxcsr_mask;
		if (mask == 0)
			mask = 0x0000ffbf;
	}
	mxcsr_feature_mask &= mask;

	/* fpu__init_system_xstate_size_legacy inlined */
	if (!cpu_feature_enabled(X86_FEATURE_FPU)) {
		size = sizeof(struct swregs_state);
	} else if (cpu_feature_enabled(X86_FEATURE_FXSR)) {
		size = sizeof(struct fxregs_state);
		fpu_user_cfg.legacy_features = XFEATURE_MASK_FPSSE;
	} else {
		size = sizeof(struct fregs_state);
		fpu_user_cfg.legacy_features = XFEATURE_MASK_FP;
	}
	fpu_kernel_cfg.max_size = size;
	fpu_kernel_cfg.default_size = size;
	fpu_user_cfg.max_size = size;
	fpu_user_cfg.default_size = size;
	fpstate_reset(&current->thread.fpu);

	/* fpu__init_task_struct_size inlined */
	task_size = sizeof(struct task_struct);
	task_size -= sizeof(current->thread.fpu.__fpstate.regs);
	task_size += fpu_kernel_cfg.default_size;
	CHECK_MEMBER_AT_END_OF(struct fpu, __fpstate);
	CHECK_MEMBER_AT_END_OF(struct thread_struct, fpu);
	CHECK_MEMBER_AT_END_OF(struct task_struct, thread);
	arch_task_struct_size = task_size;

	init_fpstate.size = fpu_kernel_cfg.max_size;
	init_fpstate.xfeatures = fpu_kernel_cfg.max_features;
}
