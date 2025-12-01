#include <asm/fpu/api.h>
#include <asm/fpu/regset.h>
#include <asm/fpu/sched.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/types.h>
#include <asm/traps.h>
#include <asm/irq_regs.h>

/* Removed: #include <uapi/asm/kvm.h> - not needed for minimal kernel */

#include <linux/hardirq.h>
#include <linux/pkeys.h>
#include <linux/vmalloc.h>

#include "context.h"
#include "internal.h"
#include "legacy.h"
#include "xstate.h"

#include <asm/trace/fpu.h>


struct fpu_state_config	fpu_kernel_cfg __ro_after_init;
struct fpu_state_config fpu_user_cfg __ro_after_init;

struct fpstate init_fpstate __ro_after_init;

static DEFINE_PER_CPU(bool, in_kernel_fpu);

DEFINE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

bool irq_fpu_usable(void)
{
	if (WARN_ON_ONCE(in_nmi()))
		return false;

	 
	if (this_cpu_read(in_kernel_fpu))
		return false;

	 
	if (!in_hardirq())
		return true;

	 
	return !softirq_count();
}

static void update_avx_timestamp(struct fpu *fpu)
{

#define AVX512_TRACKING_MASK	(XFEATURE_MASK_ZMM_Hi256 | XFEATURE_MASK_Hi16_ZMM)

	if (fpu->fpstate->regs.xsave.header.xfeatures & AVX512_TRACKING_MASK)
		fpu->avx512_timestamp = jiffies;
}

void save_fpregs_to_fpstate(struct fpu *fpu)
{
	if (likely(use_xsave())) {
		os_xsave(fpu->fpstate);
		update_avx_timestamp(fpu);
		return;
	}

	if (likely(use_fxsr())) {
		fxsave(&fpu->fpstate->regs.fxsave);
		return;
	}

	 
	asm volatile("fnsave %[fp]; fwait" : [fp] "=m" (fpu->fpstate->regs.fsave));
	frstor(&fpu->fpstate->regs.fsave);
}

void restore_fpregs_from_fpstate(struct fpstate *fpstate, u64 mask)
{
	 
	if (unlikely(static_cpu_has_bug(X86_BUG_FXSAVE_LEAK))) {
		asm volatile(
			"fnclex\n\t"
			"emms\n\t"
			"fildl %P[addr]"	 
			: : [addr] "m" (fpstate));
	}

	if (use_xsave()) {
		 
		xfd_update_state(fpstate);

		 
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

#if IS_ENABLED(CONFIG_KVM)
static void __fpstate_reset(struct fpstate *fpstate, u64 xfd);

static void fpu_init_guest_permissions(struct fpu_guest *gfpu)
{
	struct fpu_state_perm *fpuperm;
	u64 perm;

	if (!IS_ENABLED(CONFIG_X86_64))
		return;

	spin_lock_irq(&current->sighand->siglock);
	fpuperm = &current->group_leader->thread.fpu.guest_perm;
	perm = fpuperm->__state_perm;

	 
	WRITE_ONCE(fpuperm->__state_perm, perm | FPU_GUEST_PERM_LOCKED);

	spin_unlock_irq(&current->sighand->siglock);

	gfpu->perm = perm & ~FPU_GUEST_PERM_LOCKED;
}

bool fpu_alloc_guest_fpstate(struct fpu_guest *gfpu)
{
	/* Stub: KVM guest FPU not needed for minimal kernel */
	return false;
}

void fpu_free_guest_fpstate(struct fpu_guest *gfpu)
{
	/* Stub: KVM guest FPU not needed for minimal kernel */
}

int fpu_enable_guest_xfd_features(struct fpu_guest *guest_fpu, u64 xfeatures)
{
	/* Stub: KVM guest FPU not needed for minimal kernel */
	return -ENOSYS;
}


int fpu_swap_kvm_fpstate(struct fpu_guest *guest_fpu, bool enter_guest)
{
	/* Stub: KVM guest FPU not needed for minimal kernel */
	return -ENOSYS;
}

void fpu_copy_guest_fpstate_to_uabi(struct fpu_guest *gfpu, void *buf,
				    unsigned int size, u32 pkru)
{
	/* Stub: KVM guest FPU not needed for minimal kernel */
}

int fpu_copy_uabi_to_guest_fpstate(struct fpu_guest *gfpu, const void *buf,
				   u64 xcr0, u32 *vpkru)
{
	/* Stub: KVM guest FPU not needed for minimal kernel */
	return -ENOSYS;
}
#endif  

void kernel_fpu_begin_mask(unsigned int kfpu_mask)
{
	preempt_disable();

	WARN_ON_FPU(!irq_fpu_usable());
	WARN_ON_FPU(this_cpu_read(in_kernel_fpu));

	this_cpu_write(in_kernel_fpu, true);

	if (!(current->flags & PF_KTHREAD) &&
	    !test_thread_flag(TIF_NEED_FPU_LOAD)) {
		set_thread_flag(TIF_NEED_FPU_LOAD);
		save_fpregs_to_fpstate(&current->thread.fpu);
	}
	__cpu_invalidate_fpregs_state();

	 
	if (likely(kfpu_mask & KFPU_MXCSR) && boot_cpu_has(X86_FEATURE_XMM))
		ldmxcsr(MXCSR_DEFAULT);

	if (unlikely(kfpu_mask & KFPU_387) && boot_cpu_has(X86_FEATURE_FPU))
		asm volatile ("fninit");
}

void kernel_fpu_end(void)
{
	WARN_ON_FPU(!this_cpu_read(in_kernel_fpu));

	this_cpu_write(in_kernel_fpu, false);
	preempt_enable();
}

void fpu_sync_fpstate(struct fpu *fpu)
{
	WARN_ON_FPU(fpu != &current->thread.fpu);

	fpregs_lock();
	trace_x86_fpu_before_save(fpu);

	if (!test_thread_flag(TIF_NEED_FPU_LOAD))
		save_fpregs_to_fpstate(fpu);

	trace_x86_fpu_after_save(fpu);
	fpregs_unlock();
}

static inline unsigned int init_fpstate_copy_size(void)
{
	if (!use_xsave())
		return fpu_kernel_cfg.default_size;

	 
	return sizeof(init_fpstate.regs.xsave);
}

static inline void fpstate_init_fxstate(struct fpstate *fpstate)
{
	fpstate->regs.fxsave.cwd = 0x37f;
	fpstate->regs.fxsave.mxcsr = MXCSR_DEFAULT;
}

static inline void fpstate_init_fstate(struct fpstate *fpstate)
{
	fpstate->regs.fsave.cwd = 0xffff037fu;
	fpstate->regs.fsave.swd = 0xffff0000u;
	fpstate->regs.fsave.twd = 0xffffffffu;
	fpstate->regs.fsave.fos = 0xffff0000u;
}

void fpstate_init_user(struct fpstate *fpstate)
{
	if (!cpu_feature_enabled(X86_FEATURE_FPU)) {
		fpstate_init_soft(&fpstate->regs.soft);
		return;
	}

	xstate_init_xcomp_bv(&fpstate->regs.xsave, fpstate->xfeatures);

	if (cpu_feature_enabled(X86_FEATURE_FXSR))
		fpstate_init_fxstate(fpstate);
	else
		fpstate_init_fstate(fpstate);
}

static void __fpstate_reset(struct fpstate *fpstate, u64 xfd)
{
	 
	fpstate->size		= fpu_kernel_cfg.default_size;
	fpstate->user_size	= fpu_user_cfg.default_size;
	fpstate->xfeatures	= fpu_kernel_cfg.default_features;
	fpstate->user_xfeatures	= fpu_user_cfg.default_features;
	fpstate->xfd		= xfd;
}

void fpstate_reset(struct fpu *fpu)
{
	 
	fpu->fpstate = &fpu->__fpstate;
	__fpstate_reset(fpu->fpstate, init_fpstate.xfd);

	 
	fpu->perm.__state_perm		= fpu_kernel_cfg.default_features;
	fpu->perm.__state_size		= fpu_kernel_cfg.default_size;
	fpu->perm.__user_state_size	= fpu_user_cfg.default_size;
	 
	fpu->guest_perm = fpu->perm;
}

static inline void fpu_inherit_perms(struct fpu *dst_fpu)
{
	if (fpu_state_size_dynamic()) {
		struct fpu *src_fpu = &current->group_leader->thread.fpu;

		spin_lock_irq(&current->sighand->siglock);
		 
		dst_fpu->perm = src_fpu->perm;
		dst_fpu->guest_perm = src_fpu->guest_perm;
		spin_unlock_irq(&current->sighand->siglock);
	}
}

int fpu_clone(struct task_struct *dst, unsigned long clone_flags, bool minimal)
{
	struct fpu *src_fpu = &current->thread.fpu;
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
	if (!(clone_flags & CLONE_THREAD))
		fpu_inherit_perms(dst_fpu);
	fpregs_unlock();

	 
	if (use_xsave())
		dst_fpu->fpstate->regs.xsave.header.xfeatures &= ~XFEATURE_MASK_PASID;

	trace_x86_fpu_copy_src(src_fpu);
	trace_x86_fpu_copy_dst(dst_fpu);

	return 0;
}

/* Stub: fpu_thread_struct_whitelist not used externally */
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
			     "2:\n"
			     _ASM_EXTABLE(1b, 2b));
		fpregs_deactivate(fpu);
	}

	trace_x86_fpu_dropped(fpu);

	preempt_enable();
}

static inline void restore_fpregs_from_init_fpstate(u64 features_mask)
{
	if (use_xsave())
		os_xrstor(&init_fpstate, features_mask);
	else if (use_fxsr())
		fxrstor(&init_fpstate.regs.fxsave);
	else
		frstor(&init_fpstate.regs.fsave);

	pkru_write_default();
}

static void fpu_reset_fpregs(void)
{
	struct fpu *fpu = &current->thread.fpu;

	fpregs_lock();
	fpu__drop(fpu);
	 
	memcpy(&fpu->fpstate->regs, &init_fpstate.regs, init_fpstate_copy_size());
	set_thread_flag(TIF_NEED_FPU_LOAD);
	fpregs_unlock();
}

void fpu__clear_user_states(struct fpu *fpu)
{
	WARN_ON_FPU(fpu != &current->thread.fpu);

	fpregs_lock();
	if (!cpu_feature_enabled(X86_FEATURE_FPU)) {
		fpu_reset_fpregs();
		fpregs_unlock();
		return;
	}

	 
	if (xfeatures_mask_supervisor() &&
	    !fpregs_state_valid(fpu, smp_processor_id()))
		os_xrstor_supervisor(fpu->fpstate);

	 
	restore_fpregs_from_init_fpstate(XFEATURE_MASK_USER_RESTORE);

	 
	fpregs_mark_activate();
	fpregs_unlock();
}

void fpu_flush_thread(void)
{
	fpstate_reset(&current->thread.fpu);
	fpu_reset_fpregs();
}
void switch_fpu_return(void)
{
	if (!static_cpu_has(X86_FEATURE_FPU))
		return;

	fpregs_restore_userregs();
}


void fpregs_mark_activate(void)
{
	struct fpu *fpu = &current->thread.fpu;

	fpregs_activate(fpu);
	fpu->last_cpu = smp_processor_id();
	clear_thread_flag(TIF_NEED_FPU_LOAD);
}


int fpu__exception_code(struct fpu *fpu, int trap_nr)
{
	int err;

	if (trap_nr == X86_TRAP_MF) {
		unsigned short cwd, swd;
		 
		if (boot_cpu_has(X86_FEATURE_FXSR)) {
			cwd = fpu->fpstate->regs.fxsave.cwd;
			swd = fpu->fpstate->regs.fxsave.swd;
		} else {
			cwd = (unsigned short)fpu->fpstate->regs.fsave.cwd;
			swd = (unsigned short)fpu->fpstate->regs.fsave.swd;
		}

		err = swd & ~cwd;
	} else {
		 
		unsigned short mxcsr = MXCSR_DEFAULT;

		if (boot_cpu_has(X86_FEATURE_XMM))
			mxcsr = fpu->fpstate->regs.fxsave.mxcsr;

		err = ~(mxcsr >> 7) & mxcsr;
	}

	if (err & 0x001) {	 
		 
		return FPE_FLTINV;
	} else if (err & 0x004) {  
		return FPE_FLTDIV;
	} else if (err & 0x008) {  
		return FPE_FLTOVF;
	} else if (err & 0x012) {  
		return FPE_FLTUND;
	} else if (err & 0x020) {  
		return FPE_FLTRES;
	}

	 
	return 0;
}
