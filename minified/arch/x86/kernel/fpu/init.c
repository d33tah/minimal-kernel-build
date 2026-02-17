#include <asm/fpu/api.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/init.h>

#include "internal.h"
#include "xstate.h"

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
	unsigned int size;
	int task_size;

	fpstate_reset(&current->thread.fpu);
	setup_force_cpu_cap(X86_FEATURE_FPU);

	fpu__init_cpu();

	fpstate_init_user(&init_fpstate);

	if (cpu_feature_enabled(X86_FEATURE_FXSR)) {
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
