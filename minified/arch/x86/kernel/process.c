#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/idle.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/pm.h>
#include <linux/tick.h>
#include <linux/random.h>
static inline void propagate_user_return_notify(struct task_struct *prev, struct task_struct *next) {}
#include <linux/dmi.h>
#include <linux/utsname.h>
static inline void boot_init_stack_canary(void) {}
/* end stackprotector.h */
#include <linux/cpuidle.h>
#include <linux/acpi.h>
#include <linux/hw_breakpoint.h>
#include <asm/cpu.h>
#include <asm/apic.h>
#include <linux/uaccess.h>
#include <asm/mwait.h>
#include <asm/fpu/api.h>
#include <asm/fpu/sched.h>
#include <asm/fpu/xstate.h>
#include <asm/debugreg.h>
#include <asm/nmi.h>
#include <asm/tlbflush.h>
#include <asm/mce.h>
#include <asm/switch_to.h>
#include <asm/desc.h>
#include <asm/prctl.h>
#include <asm/spec-ctrl.h>
#include <asm/proto.h>
/* --- 2025-12-07 20:47 --- Inlined frame.h */
#include <asm/asm.h>
#define ENCODE_FRAME_POINTER
static inline unsigned long encode_frame_pointer(struct pt_regs *regs)
{
	return 0;
}
#define FRAME_BEGIN
#define FRAME_END
#define FRAME_OFFSET 0
#include <asm/unwind.h>
#include <asm/tdx.h>

#include "process.h"

__visible DEFINE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw) = {
	.x86_tss = {
		 
		.sp0 = (1UL << (BITS_PER_LONG-1)) + 1,

		.sp1 = TOP_OF_INIT_STACK,

		.ss0 = __KERNEL_DS,
		.ss1 = __KERNEL_CS,
		.io_bitmap_base	= IO_BITMAP_OFFSET_INVALID,
	 },
};

DEFINE_PER_CPU(bool, __tss_limit_invalid);

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

	frame->bp = encode_frame_pointer(childregs);
	frame->ret_addr = (unsigned long) ret_from_fork;
	p->thread.sp = (unsigned long) fork_frame;
	memset(p->thread.ptrace_bps, 0, sizeof(p->thread.ptrace_bps));

	p->thread.sp0 = (unsigned long) (childregs + 1);
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

static void pkru_flush_thread(void)
{
	 
	pkru_write_default();
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	flush_ptrace_hw_breakpoint(tsk);
	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));

	fpu_flush_thread();
	pkru_flush_thread();
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

static void disable_cpuid(void)
{
	preempt_disable();
	if (!test_and_set_thread_flag(TIF_NOCPUID)) {
		 
		set_cpuid_faulting(true);
	}
	preempt_enable();
}

static void enable_cpuid(void)
{
	preempt_disable();
	if (test_and_clear_thread_flag(TIF_NOCPUID)) {
		 
		set_cpuid_faulting(false);
	}
	preempt_enable();
}

static int get_cpuid_mode(void)
{
	return !test_thread_flag(TIF_NOCPUID);
}

static int set_cpuid_mode(unsigned long cpuid_enabled)
{
	if (!boot_cpu_has(X86_FEATURE_CPUID_FAULT))
		return -ENODEV;

	if (cpuid_enabled)
		enable_cpuid();
	else
		disable_cpuid();

	return 0;
}

void arch_setup_new_exec(void)
{

	if (test_thread_flag(TIF_NOCPUID))
		enable_cpuid();
}

static inline void switch_to_bitmap(unsigned long tifp) { }

static __always_inline void amd_set_core_ssb_state(unsigned long tifn)
{
	u64 msr = x86_amd_ls_cfg_base | ssbd_tif_to_amd_ls_cfg(tifn);

	wrmsrl(MSR_AMD64_LS_CFG, msr);
}

static __always_inline void amd_set_ssb_virt_state(unsigned long tifn)
{
	 
	wrmsrl(MSR_AMD64_VIRT_SPEC_CTRL, ssbd_tif_to_spec_ctrl(tifn));
}

static __always_inline void __speculation_ctrl_update(unsigned long tifp,
						      unsigned long tifn)
{
	unsigned long tif_diff = tifp ^ tifn;
	u64 msr = x86_spec_ctrl_base;
	bool updmsr = false;

	lockdep_assert_irqs_disabled();

	 
	if (static_cpu_has(X86_FEATURE_VIRT_SSBD)) {
		if (tif_diff & _TIF_SSBD)
			amd_set_ssb_virt_state(tifn);
	} else if (static_cpu_has(X86_FEATURE_LS_CFG_SSBD)) {
		if (tif_diff & _TIF_SSBD)
			amd_set_core_ssb_state(tifn);
	} else if (static_cpu_has(X86_FEATURE_SPEC_CTRL_SSBD) ||
		   static_cpu_has(X86_FEATURE_AMD_SSBD)) {
		updmsr |= !!(tif_diff & _TIF_SSBD);
		msr |= ssbd_tif_to_spec_ctrl(tifn);
	}

	if (updmsr)
		write_spec_ctrl_current(msr, false);
}

void __switch_to_xtra(struct task_struct *prev_p, struct task_struct *next_p)
{
	unsigned long tifp, tifn;

	tifn = read_task_thread_flags(next_p);
	tifp = read_task_thread_flags(prev_p);

	switch_to_bitmap(tifp);

	propagate_user_return_notify(prev_p, next_p);

	if ((tifp ^ tifn) & _TIF_NOCPUID)
		set_cpuid_faulting(!!(tifn & _TIF_NOCPUID));

	__speculation_ctrl_update(tifp, tifn);
}

static void (*x86_idle)(void);

static inline void play_dead(void)
{
	BUG();
}

void arch_cpu_idle_enter(void)
{
	tsc_verify_tsc_adjust(false);
	local_touch_nmi();
}

void arch_cpu_idle_dead(void)
{
	play_dead();
}

void arch_cpu_idle(void)
{
	x86_idle();
}

void __cpuidle default_idle(void)
{
	raw_safe_halt();
}
#if defined(CONFIG_APM_MODULE) || defined(CONFIG_HALTPOLL_CPUIDLE_MODULE)
#endif


/* Simplified: just use default_idle for minimal kernel */
void select_idle_routine(const struct cpuinfo_x86 *c)
{
	if (!x86_idle)
		x86_idle = default_idle;
}


void __init arch_post_acpi_subsys_init(void)
{
	/*
	 * The only work here was the AMD C1E (Erratum 400) detection, gated on
	 * boot_cpu_has_bug(X86_BUG_AMD_E400). That bug bit is never set anywhere
	 * in this tree (all CPU-bug detection in cpu_set_bug_bits() was removed),
	 * so the guard always returned early -- the whole body was dead. The bit
	 * it would have set (X86_BUG_AMD_APIC_C1E) is likewise never read.
	 */
}


unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_int() % 8192;
	return sp & ~0xf;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	return randomize_page(mm->brk, 0x02000000);
}

long do_arch_prctl_common(int option, unsigned long arg2)
{
	switch (option) {
	case ARCH_GET_CPUID:
		return get_cpuid_mode();
	case ARCH_SET_CPUID:
		return set_cpuid_mode(arg2);
	case ARCH_GET_XCOMP_SUPP:
	case ARCH_GET_XCOMP_PERM:
	case ARCH_REQ_XCOMP_PERM:
	case ARCH_GET_XCOMP_GUEST_PERM:
	case ARCH_REQ_XCOMP_GUEST_PERM:
		return fpu_xstate_prctl(option, arg2);
	}

	return -EINVAL;
}
