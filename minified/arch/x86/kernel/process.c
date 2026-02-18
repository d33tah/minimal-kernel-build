#include <linux/ptrace.h>
/* end stackprotector.h */
#include <asm/fpu/sched.h>
/* Moved from hw_breakpoint.c - just the percpu variable needed for hw_breakpoint_active() */
DEFINE_PER_CPU(unsigned long, cpu_dr7);
#include <asm/nmi.h>
#include <asm/tlbflush.h>
#include <asm/switch_to.h>
extern u64 x86_amd_ls_cfg_base;
extern u64 x86_amd_ls_cfg_ssbd_mask;
static inline u64 ssbd_tif_to_spec_ctrl(u64 tifn)
{
	BUILD_BUG_ON(TIF_SSBD < SPEC_CTRL_SSBD_SHIFT);
	return (tifn & _TIF_SSBD) >> (TIF_SSBD - SPEC_CTRL_SSBD_SHIFT);
}
static inline u64 ssbd_tif_to_amd_ls_cfg(u64 tifn)
{
	return (tifn & _TIF_SSBD) ? x86_amd_ls_cfg_ssbd_mask : 0ULL;
}
extern void speculation_ctrl_update(unsigned long tif);

__visible DEFINE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw) = {
	.x86_tss = {
		 
		.sp0 = (1UL << (BITS_PER_LONG-1)) + 1,

		.sp1 = TOP_OF_INIT_STACK,

		.ss0 = __KERNEL_DS,
		.ss1 = __KERNEL_CS,
		.io_bitmap_base	= IO_BITMAP_OFFSET_INVALID,
	 },
};

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	memcpy(dst, src, arch_task_struct_size);

	dst->thread.fpu.fpstate = NULL;

	return 0;
}

void exit_thread(struct task_struct *tsk)
{
	struct thread_struct *t = &tsk->thread;
	struct fpu *fpu = &t->fpu;

	fpu__drop(fpu);
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	unsigned long clone_flags = args->flags;
	unsigned long sp = args->stack;
	struct inactive_task_frame *frame;
	struct fork_frame *fork_frame;
	struct pt_regs *childregs;
	int ret = 0;

	childregs = task_pt_regs(p);
	fork_frame = container_of(childregs, struct fork_frame, regs);
	frame = &fork_frame->frame;

	frame->bp = 0; /* encode_frame_pointer always returned 0 */
	frame->ret_addr = (unsigned long)ret_from_fork;
	p->thread.sp = (unsigned long)fork_frame;
	p->thread.io_bitmap = NULL;
	p->thread.iopl_warn = 0;
	p->thread.sp0 = (unsigned long)(childregs + 1);
	savesegment(gs, p->thread.gs);

	frame->flags = X86_EFLAGS_FIXED;

	fpu_clone(p, clone_flags, args->fn);

	if (unlikely(p->flags & PF_KTHREAD)) {
		p->thread.pkru = pkru_get_init_value();
		memset(childregs, 0, sizeof(struct pt_regs));
		kthread_frame_init(frame, args->fn, args->fn_arg);
		return 0;
	}

	p->thread.pkru = read_pkru();

	frame->bx = 0;
	*childregs = *current_pt_regs();
	childregs->ax = 0;
	if (sp)
		childregs->sp = sp;

	if (unlikely(args->fn)) {
		childregs->sp = 0;
		childregs->ip = 0;
		kthread_frame_init(frame, args->fn, args->fn_arg);
		return 0;
	}

	return ret;
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));

	fpu_flush_thread();
	pkru_write_default();
}

DEFINE_PER_CPU(u64, msr_misc_features_shadow);

static void set_cpuid_faulting(bool on)
{
	u64 msrval;

	msrval = this_cpu_read(msr_misc_features_shadow);
	msrval &= ~MSR_MISC_FEATURES_ENABLES_CPUID_FAULT;
	msrval |= (on << MSR_MISC_FEATURES_ENABLES_CPUID_FAULT_BIT);
	this_cpu_write(msr_misc_features_shadow, msrval);
	wrmsrl(MSR_MISC_FEATURES_ENABLES, msrval);
}

void arch_setup_new_exec(void)
{
	if (test_thread_flag(TIF_NOCPUID)) {
		preempt_disable();
		if (test_and_clear_thread_flag(TIF_NOCPUID))
			set_cpuid_faulting(false);
		preempt_enable();
	}

	if (test_thread_flag(TIF_SSBD) && task_spec_ssb_noexec(current)) {
		clear_thread_flag(TIF_SSBD);
		task_clear_spec_ssb_disable(current);
		task_clear_spec_ssb_noexec(current);
		speculation_ctrl_update(read_thread_flags());
	}
}

static __always_inline void __speculation_ctrl_update(unsigned long tifp,
						      unsigned long tifn)
{
	unsigned long tif_diff = tifp ^ tifn;

	if (static_cpu_has(X86_FEATURE_VIRT_SSBD)) {
		if (tif_diff & _TIF_SSBD)
			wrmsrl(MSR_AMD64_VIRT_SPEC_CTRL,
			       ssbd_tif_to_spec_ctrl(tifn));
	} else if (static_cpu_has(X86_FEATURE_LS_CFG_SSBD)) {
		if (tif_diff & _TIF_SSBD)
			wrmsrl(MSR_AMD64_LS_CFG,
			       x86_amd_ls_cfg_base |
				       ssbd_tif_to_amd_ls_cfg(tifn));
	}
}

static unsigned long speculation_ctrl_update_tif(struct task_struct *tsk)
{
	if (test_and_clear_ti_thread_flag(task_thread_info(tsk),
					  TIF_SPEC_FORCE_UPDATE)) {
		if (task_spec_ssb_disable(tsk))
			set_tsk_thread_flag(tsk, TIF_SSBD);
		else
			clear_tsk_thread_flag(tsk, TIF_SSBD);

		if (task_spec_ib_disable(tsk))
			set_tsk_thread_flag(tsk, TIF_SPEC_IB);
		else
			clear_tsk_thread_flag(tsk, TIF_SPEC_IB);
	}

	return read_task_thread_flags(tsk);
}

void speculation_ctrl_update(unsigned long tif)
{
	unsigned long flags;

	local_irq_save(flags);
	__speculation_ctrl_update(~tif, tif);
	local_irq_restore(flags);
}

void __switch_to_xtra(struct task_struct *prev_p, struct task_struct *next_p)
{
	unsigned long tifp, tifn;

	tifn = read_task_thread_flags(next_p);
	tifp = read_task_thread_flags(prev_p);

	if ((tifp & _TIF_BLOCKSTEP || tifn & _TIF_BLOCKSTEP) &&
	    arch_has_block_step()) {
		unsigned long debugctl, msk;

		rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
		debugctl &= ~DEBUGCTLMSR_BTF;
		msk = tifn & _TIF_BLOCKSTEP;
		debugctl |= (msk >> TIF_BLOCKSTEP) << DEBUGCTLMSR_BTF_SHIFT;
		wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	}

	if ((tifp ^ tifn) & _TIF_NOTSC) {
		unsigned long newval, cr4 = this_cpu_read(cpu_tlbstate.cr4);
		newval = cr4 ^ X86_CR4_TSD;
		if (newval != cr4) {
			this_cpu_write(cpu_tlbstate.cr4, newval);
			__write_cr4(newval);
		}
	}

	if ((tifp ^ tifn) & _TIF_NOCPUID)
		set_cpuid_faulting(!!(tifn & _TIF_NOCPUID));

	if (likely(!((tifp | tifn) & _TIF_SPEC_FORCE_UPDATE))) {
		__speculation_ctrl_update(tifp, tifn);
	} else {
		speculation_ctrl_update_tif(prev_p);
		tifn = speculation_ctrl_update_tif(next_p);

		__speculation_ctrl_update(~tifn, tifn);
	}
}

static void (*x86_idle)(void);

void arch_cpu_idle_enter(void)
{
	local_touch_nmi();
}

void arch_cpu_idle(void)
{
	x86_idle();
}

void __cpuidle default_idle(void)
{
	raw_safe_halt();
}

void select_idle_routine(const struct cpuinfo_x86 *c)
{
	if (!x86_idle)
		x86_idle = default_idle;
}

void __init arch_post_acpi_subsys_init(void)
{
}

unsigned long arch_align_stack(unsigned long sp)
{
	return sp & ~0xf;
}
