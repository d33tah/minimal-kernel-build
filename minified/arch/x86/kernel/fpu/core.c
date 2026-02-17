#include <asm/fpu/api.h>
#include <asm/fpu/sched.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/types.h>
#include <asm/traps.h>
#include <asm/irq_regs.h>

#include <linux/hardirq.h>

#include <asm/fpu/xstate.h>

static inline int fpregs_state_valid(struct fpu *fpu, unsigned int cpu)
{
	return fpu == this_cpu_read(fpu_fpregs_owner_ctx) &&
	       cpu == fpu->last_cpu;
}

static inline void fpregs_deactivate(struct fpu *fpu)
{
	__this_cpu_write(fpu_fpregs_owner_ctx, NULL);
}

static inline void fpregs_activate(struct fpu *fpu)
{
	__this_cpu_write(fpu_fpregs_owner_ctx, fpu);
}

static inline void fpregs_restore_userregs(void)
{
	struct fpu *fpu = &current->thread.fpu;
	int cpu = smp_processor_id();

	if (WARN_ON_ONCE(current->flags & PF_KTHREAD))
		return;

	if (!fpregs_state_valid(fpu, cpu)) {
		restore_fpregs_from_fpstate(fpu->fpstate,
					    XFEATURE_MASK_FPSTATE);

		fpregs_activate(fpu);
		fpu->last_cpu = cpu;
	}
	clear_thread_flag(TIF_NEED_FPU_LOAD);
}
/* end context.h */
#include "internal.h"
#define kernel_insn(insn, output, input...)                                \
	asm volatile("1:" #insn "\n\t"                                     \
		     "2:\n" _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FPU_RESTORE) \
		     : output                                              \
		     : input)
// clang-format off
static inline void fxrstor(struct fxregs_state *fx)
{
	kernel_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}
static inline void frstor(struct fregs_state *fx)
{
	kernel_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}
static inline void fxsave(struct fxregs_state *fx)
{
	asm volatile("fxsave %[fx]" : [fx] "=m" (*fx));
}
// clang-format on
#include "xstate.h"

struct fpu_state_config fpu_kernel_cfg __ro_after_init;
struct fpu_state_config fpu_user_cfg __ro_after_init;

struct fpstate init_fpstate __ro_after_init;

DEFINE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

void save_fpregs_to_fpstate(struct fpu *fpu)
{
	if (likely(use_xsave())) {
		os_xsave(fpu->fpstate);
		return;
	}

	if (likely(use_fxsr())) {
		fxsave(&fpu->fpstate->regs.fxsave);
		return;
	}

	asm volatile("fnsave %[fp]; fwait"
		     : [fp] "=m"(fpu->fpstate->regs.fsave));
	frstor(&fpu->fpstate->regs.fsave);
}

void restore_fpregs_from_fpstate(struct fpstate *fpstate, u64 mask)
{
	if (unlikely(static_cpu_has_bug(X86_BUG_FXSAVE_LEAK))) {
		asm volatile("fnclex\n\t"
			     "emms\n\t"
			     "fildl %P[addr]"
			     :
			     : [addr] "m"(fpstate));
	}

	if (use_xsave()) {
		mask = fpu_kernel_cfg.max_features & mask;

		os_xrstor(fpstate, mask);
	} else {
		if (use_fxsr())
			fxrstor(&fpstate->regs.fxsave);
		else
			frstor(&fpstate->regs.fsave);
	}
}

void fpu_reset_from_exception_fixup(void)
{
	restore_fpregs_from_fpstate(&init_fpstate, XFEATURE_MASK_FPSTATE);
}

void fpu_sync_fpstate(struct fpu *fpu)
{
	WARN_ON_FPU(fpu != &current->thread.fpu);

	fpregs_lock();
	if (!test_thread_flag(TIF_NEED_FPU_LOAD))
		save_fpregs_to_fpstate(fpu);
	fpregs_unlock();
}

static inline unsigned int init_fpstate_copy_size(void)
{
	if (!use_xsave())
		return fpu_kernel_cfg.default_size;

	return sizeof(init_fpstate.regs.xsave);
}

void fpstate_init_user(struct fpstate *fpstate)
{
	if (!cpu_feature_enabled(X86_FEATURE_FPU))
		return;

	if (cpu_feature_enabled(X86_FEATURE_XCOMPACTED))
		fpstate->regs.xsave.header.xcomp_bv = fpstate->xfeatures |
						      XCOMP_BV_COMPACTED_FORMAT;

	if (cpu_feature_enabled(X86_FEATURE_FXSR)) {
		fpstate->regs.fxsave.cwd = 0x37f;
		fpstate->regs.fxsave.mxcsr = MXCSR_DEFAULT;
	} else {
		fpstate->regs.fsave.cwd = 0xffff037fu;
		fpstate->regs.fsave.swd = 0xffff0000u;
		fpstate->regs.fsave.twd = 0xffffffffu;
		fpstate->regs.fsave.fos = 0xffff0000u;
	}
}

void fpstate_reset(struct fpu *fpu)
{
	struct fpstate *fpstate;
	fpu->fpstate = &fpu->__fpstate;
	fpstate = fpu->fpstate;
	fpstate->size = fpu_kernel_cfg.default_size;
	fpstate->user_size = fpu_user_cfg.default_size;
	fpstate->xfeatures = fpu_kernel_cfg.default_features;
}

int fpu_clone(struct task_struct *dst, unsigned long clone_flags, bool minimal)
{
	struct fpu *dst_fpu = &dst->thread.fpu;

	dst_fpu->last_cpu = -1;

	fpstate_reset(dst_fpu);

	if (!cpu_feature_enabled(X86_FEATURE_FPU))
		return 0;

	set_tsk_thread_flag(dst, TIF_NEED_FPU_LOAD);

	if (minimal) {
		memcpy(&dst_fpu->fpstate->regs, &init_fpstate.regs,
		       init_fpstate_copy_size());
		return 0;
	}

	BUILD_BUG_ON(XFEATURE_MASK_USER_DYNAMIC != XFEATURE_MASK_XTILE_DATA);

	fpregs_lock();
	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		fpregs_restore_userregs();
	save_fpregs_to_fpstate(dst_fpu);
	fpregs_unlock();

	if (use_xsave())
		dst_fpu->fpstate->regs.xsave.header.xfeatures &=
			~XFEATURE_MASK_PASID;

	return 0;
}

void fpu_thread_struct_whitelist(unsigned long *offset, unsigned long *size)
{
	*offset = 0;
	*size = 0;
}

void fpu__drop(struct fpu *fpu)
{
	preempt_disable();

	if (fpu == &current->thread.fpu) {
		asm volatile("1: fwait\n"
			     "2:\n" _ASM_EXTABLE(1b, 2b));
		fpregs_deactivate(fpu);
	}

	preempt_enable();
}

void fpu_flush_thread(void)
{
	struct fpu *fpu = &current->thread.fpu;

	fpstate_reset(fpu);
	fpregs_lock();
	fpu__drop(fpu);
	memcpy(&fpu->fpstate->regs, &init_fpstate.regs,
	       init_fpstate_copy_size());
	set_thread_flag(TIF_NEED_FPU_LOAD);
	fpregs_unlock();
}
void switch_fpu_return(void)
{
	if (!static_cpu_has(X86_FEATURE_FPU))
		return;

	fpregs_restore_userregs();
}

int fpu__exception_code(struct fpu *fpu, int trap_nr)
{
	return 0;
}
