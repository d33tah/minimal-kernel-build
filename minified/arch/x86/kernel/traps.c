/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * Handle hardware traps and faults.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kprobes.h>
#include <asm/bug.h>
#include <asm/cpufeatures.h>
#include <asm/page_types.h>
#include <asm/signal.h>
#include <linux/rcupdate.h>
#include <linux/uaccess.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/uprobes.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/debugreg.h>
#include <asm/text-patching.h>
#include <asm/traps.h>
#include <asm/desc.h>
#include <asm/fpu/api.h>
#include <asm/cpu.h>
#include <asm/cpu_entry_area.h>
#include <asm/fpu/xstate.h>
#include <asm/vm86.h>
#include <asm/umip.h>
#include <asm/insn.h>
#include <asm/insn-eval.h>
#include <asm/vdso.h>
#include <asm/proto.h>

#include "asm-generic/errno-base.h"
#include "asm-generic/int-ll64.h"
#include "asm-generic/kprobes.h"
#include "asm-generic/siginfo.h"
#include "asm/bug.h"
#include "asm/cpufeature.h"
#include "asm/cpufeatures.h"
#include "asm/current.h"
#include "asm/debugreg.h"
#include "asm/extable.h"
#include "asm/idtentry.h"
#include "asm/irq_vectors.h"
#include "asm/kdebug.h"
#include "asm/mem_encrypt.h"
#include "asm/msr-index.h"
#include "asm/msr.h"
#include "asm/processor-flags.h"
#include "asm/ptrace.h"
#include "asm/special_insns.h"
#include "asm/thread_info.h"
#include "asm/trapnr.h"
#include "asm/uaccess.h"
#include "linux/compiler_attributes.h"
#include "linux/compiler_types.h"
#include "linux/cpu.h"
#include "linux/entry-common.h"
#include "linux/instrumentation.h"
#include "linux/irqflags.h"
#include "linux/kconfig.h"
#include "linux/kern_levels.h"
#include "linux/notifier.h"
#include "linux/panic.h"
#include "linux/preempt.h"
#include "linux/printk.h"
#include "linux/rcupdate.h"
#include "linux/sched/signal.h"
#include "linux/signal.h"
#include "linux/stddef.h"
#include "linux/thread_info.h"
#include "linux/types.h"

DECLARE_BITMAP(system_vectors, NR_VECTORS);

static inline void cond_local_irq_enable(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void cond_local_irq_disable(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_disable();
}

__always_inline int is_valid_bugaddr(unsigned long addr)
{
	if (addr < TASK_SIZE_MAX)
		return 0;

	/*
	 * We got #UD, if the text isn't readable we'd have gotten
	 * a different exception.
	 */
	return *(unsigned short *)addr == INSN_UD2;
}

static nokprobe_inline int
do_trap_no_signal(struct task_struct *tsk, int trapnr, const char *str,
		  struct pt_regs *regs,	long error_code)
{
	if (v8086_mode(regs)) {
		/*
		 * Traps 0, 1, 3, 4, and 5 should be forwarded to vm86.
		 * On nmi (interrupt 2), do_trap should not be called.
		 */
		if (trapnr < X86_TRAP_UD) {
			if (!handle_vm86_trap((struct kernel_vm86_regs *) regs,
						error_code, trapnr))
				return 0;
		}
	} else if (!user_mode(regs)) {
		if (fixup_exception(regs, trapnr, error_code, 0))
			return 0;

		tsk->thread.error_code = error_code;
		tsk->thread.trap_nr = trapnr;
		die(str, regs, error_code);
	} else {
		if (fixup_vdso_exception(regs, trapnr, error_code, 0))
			return 0;
	}

	/*
	 * We want error_code and trap_nr set for userspace faults and
	 * kernelspace faults which result in die(), but not
	 * kernelspace faults which are fixed up.  die() gives the
	 * process no chance to handle the signal and notice the
	 * kernel fault information, so that won't result in polluting
	 * the information about previously queued, but not yet
	 * delivered, faults.  See also exc_general_protection below.
	 */
	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = trapnr;

	return -1;
}

static void show_signal(struct task_struct *tsk, int signr,
			const char *type, const char *desc,
			struct pt_regs *regs, long error_code)
{
	if (show_unhandled_signals && unhandled_signal(tsk, signr) &&
	    printk_ratelimit()) {
		pr_info("%s[%d] %s%s ip:%lx sp:%lx error:%lx",
			tsk->comm, task_pid_nr(tsk), type, desc,
			regs->ip, regs->sp, error_code);
		print_vma_addr(KERN_CONT " in ", regs->ip);
		pr_cont("\n");
	}
}

static void
do_trap(int trapnr, int signr, char *str, struct pt_regs *regs,
	long error_code, int sicode, void __user *addr)
{
	struct task_struct *tsk = current;

	if (!do_trap_no_signal(tsk, trapnr, str, regs, error_code))
		return;

	show_signal(tsk, signr, "trap ", str, regs, error_code);

	if (!sicode)
		force_sig(signr);
	else
		force_sig_fault(signr, sicode, addr);
}
NOKPROBE_SYMBOL(do_trap);

static void do_error_trap(struct pt_regs *regs, long error_code, char *str,
	unsigned long trapnr, int signr, int sicode, void __user *addr)
{
	RCU_LOCKDEP_WARN(!rcu_is_watching(), "entry code didn't wake RCU");

	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) !=
			NOTIFY_STOP) {
		cond_local_irq_enable(regs);
		do_trap(trapnr, signr, str, regs, error_code, sicode, addr);
		cond_local_irq_disable(regs);
	}
}

/*
 * Posix requires to provide the address of the faulting instruction for
 * SIGILL (#UD) and SIGFPE (#DE) in the si_addr member of siginfo_t.
 *
 * This address is usually regs->ip, but when an uprobe moved the code out
 * of line then regs->ip points to the XOL code which would confuse
 * anything which analyzes the fault address vs. the unmodified binary. If
 * a trap happened in XOL code then uprobe maps regs->ip back to the
 * original instruction address.
 */
static __always_inline void __user *error_get_trap_addr(struct pt_regs *regs)
{
	return (void __user *)uprobe_get_trap_addr(regs);
}

DEFINE_IDTENTRY(exc_divide_error)
{
	do_error_trap(regs, 0, "divide error", X86_TRAP_DE, SIGFPE,
		      FPE_INTDIV, error_get_trap_addr(regs));
}

DEFINE_IDTENTRY(exc_overflow)
{
	do_error_trap(regs, 0, "overflow", X86_TRAP_OF, SIGSEGV, 0, NULL);
}


static inline void handle_invalid_op(struct pt_regs *regs)
{
	do_error_trap(regs, 0, "invalid opcode", X86_TRAP_UD, SIGILL,
		      ILL_ILLOPN, error_get_trap_addr(regs));
}

static noinstr bool handle_bug(struct pt_regs *regs)
{
	bool handled = false;

	if (!is_valid_bugaddr(regs->ip))
		return handled;

	/*
	 * All lies, just get the WARN/BUG out.
	 */
	instrumentation_begin();
	/*
	 * Since we're emulating a CALL with exceptions, restore the interrupt
	 * state to what it was at the exception site.
	 */
	if (regs->flags & X86_EFLAGS_IF)
		raw_local_irq_enable();
	if (report_bug(regs->ip, regs) == BUG_TRAP_TYPE_WARN) {
		regs->ip += LEN_UD2;
		handled = true;
	}
	if (regs->flags & X86_EFLAGS_IF)
		raw_local_irq_disable();
	instrumentation_end();

	return handled;
}

DEFINE_IDTENTRY_RAW(exc_invalid_op)
{
	irqentry_state_t state;

	/*
	 * We use UD2 as a short encoding for 'CALL __WARN', as such
	 * handle it before exception entry to avoid recursive WARN
	 * in case exception entry is the one triggering WARNs.
	 */
	if (!user_mode(regs) && handle_bug(regs))
		return;

	state = irqentry_enter(regs);
	instrumentation_begin();
	handle_invalid_op(regs);
	instrumentation_end();
	irqentry_exit(regs, state);
}

DEFINE_IDTENTRY(exc_coproc_segment_overrun)
{
	do_error_trap(regs, 0, "coprocessor segment overrun",
		      X86_TRAP_OLD_MF, SIGFPE, 0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_invalid_tss)
{
	do_error_trap(regs, error_code, "invalid TSS", X86_TRAP_TS, SIGSEGV,
		      0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_segment_not_present)
{
	do_error_trap(regs, error_code, "segment not present", X86_TRAP_NP,
		      SIGBUS, 0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_stack_segment)
{
	do_error_trap(regs, error_code, "stack segment", X86_TRAP_SS, SIGBUS,
		      0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_alignment_check)
{
	char *str = "alignment check";

	if (notify_die(DIE_TRAP, str, regs, error_code, X86_TRAP_AC, SIGBUS) == NOTIFY_STOP)
		return;

	if (!user_mode(regs))
		die("Split lock detected\n", regs, error_code);

	local_irq_enable();

	if (handle_user_split_lock(regs, error_code))
		goto out;

	do_trap(X86_TRAP_AC, SIGBUS, "alignment check", regs,
		error_code, BUS_ADRALN, NULL);

out:
	local_irq_disable();
}


/*
 * Runs on an IST stack for x86_64 and on a special task stack for x86_32.
 *
 * On x86_64, this is more or less a normal kernel entry.  Notwithstanding the
 * SDM's warnings about double faults being unrecoverable, returning works as
 * expected.  Presumably what the SDM actually means is that the CPU may get
 * the register state wrong on entry, so returning could be a bad idea.
 *
 * Various CPU engineers have promised that double faults due to an IRET fault
 * while the stack is read-only are, in fact, recoverable.
 *
 * On x86_32, this is entered through a task gate, and regs are synthesized
 * from the TSS.  Returning is, in principle, okay, but changes to regs will
 * be lost.  If, for some reason, we need to return to a context with modified
 * regs, the shim code could be adjusted to synchronize the registers.
 *
 * The 32bit #DF shim provides CR2 already as an argument. On 64bit it needs
 * to be read before doing anything else.
 */
DEFINE_IDTENTRY_DF(exc_double_fault)
{
	static const char str[] = "double fault";
	struct task_struct *tsk = current;



	irqentry_nmi_enter(regs);
	instrumentation_begin();
	notify_die(DIE_TRAP, str, regs, error_code, X86_TRAP_DF, SIGSEGV);

	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = X86_TRAP_DF;


	pr_emerg("PANIC: double fault, error_code: 0x%lx\n", error_code);
	die("double fault", regs, error_code);
	panic("Machine halted.");
	instrumentation_end();
}

DEFINE_IDTENTRY(exc_bounds)
{
	if (notify_die(DIE_TRAP, "bounds", regs, 0,
			X86_TRAP_BR, SIGSEGV) == NOTIFY_STOP)
		return;
	cond_local_irq_enable(regs);

	if (!user_mode(regs))
		die("bounds", regs, 0);

	do_trap(X86_TRAP_BR, SIGSEGV, "bounds", regs, 0, 0, NULL);

	cond_local_irq_disable(regs);
}

enum kernel_gp_hint {
	GP_NO_HINT,
	GP_NON_CANONICAL,
	GP_CANONICAL
};

/*
 * When an uncaught #GP occurs, try to determine the memory address accessed by
 * the instruction and return that address to the caller. Also, try to figure
 * out whether any part of the access to that address was non-canonical.
 */
static enum kernel_gp_hint get_kernel_gp_address(struct pt_regs *regs,
						 unsigned long *addr)
{
	u8 insn_buf[MAX_INSN_SIZE];
	struct insn insn;
	int ret;

	if (copy_from_kernel_nofault(insn_buf, (void *)regs->ip,
			MAX_INSN_SIZE))
		return GP_NO_HINT;

	ret = insn_decode_kernel(&insn, insn_buf);
	if (ret < 0)
		return GP_NO_HINT;

	*addr = (unsigned long)insn_get_addr_ref(&insn, regs);
	if (*addr == -1UL)
		return GP_NO_HINT;


	return GP_CANONICAL;
}

#define GPFSTR "general protection fault"

static bool fixup_iopl_exception(struct pt_regs *regs)
{
	struct thread_struct *t = &current->thread;
	unsigned char byte;
	unsigned long ip;

	if (!IS_ENABLED(CONFIG_X86_IOPL_IOPERM) || t->iopl_emul != 3)
		return false;

	if (insn_get_effective_ip(regs, &ip))
		return false;

	if (get_user(byte, (const char __user *)ip))
		return false;

	if (byte != 0xfa && byte != 0xfb)
		return false;

	if (!t->iopl_warn && printk_ratelimit()) {
		pr_err("%s[%d] attempts to use CLI/STI, pretending it's a NOP, ip:%lx",
		       current->comm, task_pid_nr(current), ip);
		print_vma_addr(KERN_CONT " in ", ip);
		pr_cont("\n");
		t->iopl_warn = 1;
	}

	regs->ip += 1;
	return true;
}

/*
 * The unprivileged ENQCMD instruction generates #GPs if the
 * IA32_PASID MSR has not been populated.  If possible, populate
 * the MSR from a PASID previously allocated to the mm.
 */
static bool try_fixup_enqcmd_gp(void)
{
	return false;
}

static bool gp_try_fixup_and_notify(struct pt_regs *regs, int trapnr,
				    unsigned long error_code, const char *str)
{
	if (fixup_exception(regs, trapnr, error_code, 0))
		return true;

	current->thread.error_code = error_code;
	current->thread.trap_nr = trapnr;

	/*
	 * To be potentially processing a kprobe fault and to trust the result
	 * from kprobe_running(), we have to be non-preemptible.
	 */
	if (!preemptible() && kprobe_running() &&
	    kprobe_fault_handler(regs, trapnr))
		return true;

	return notify_die(DIE_GPF, str, regs, error_code, trapnr, SIGSEGV) == NOTIFY_STOP;
}

static void gp_user_force_sig_segv(struct pt_regs *regs, int trapnr,
				   unsigned long error_code, const char *str)
{
	current->thread.error_code = error_code;
	current->thread.trap_nr = trapnr;
	show_signal(current, SIGSEGV, "", str, regs, error_code);
	force_sig(SIGSEGV);
}

DEFINE_IDTENTRY_ERRORCODE(exc_general_protection)
{
	char desc[sizeof(GPFSTR) + 50 + 2*sizeof(unsigned long) + 1] = GPFSTR;
	enum kernel_gp_hint hint = GP_NO_HINT;
	unsigned long gp_addr;

	if (user_mode(regs) && try_fixup_enqcmd_gp())
		return;

	cond_local_irq_enable(regs);

	if (static_cpu_has(X86_FEATURE_UMIP)) {
		if (user_mode(regs) && fixup_umip_exception(regs))
			goto exit;
	}

	if (v8086_mode(regs)) {
		local_irq_enable();
		handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
		local_irq_disable();
		return;
	}

	if (user_mode(regs)) {
		if (fixup_iopl_exception(regs))
			goto exit;

		if (fixup_vdso_exception(regs, X86_TRAP_GP, error_code, 0))
			goto exit;

		gp_user_force_sig_segv(regs, X86_TRAP_GP, error_code, desc);
		goto exit;
	}

	if (gp_try_fixup_and_notify(regs, X86_TRAP_GP, error_code, desc))
		goto exit;

	if (error_code)
		snprintf(desc, sizeof(desc), "segment-related " GPFSTR);
	else
		hint = get_kernel_gp_address(regs, &gp_addr);

	if (hint != GP_NO_HINT)
		snprintf(desc, sizeof(desc), GPFSTR ", %s 0x%lx",
			 (hint == GP_NON_CANONICAL) ? "probably for non-canonical address"
						    : "maybe for address",
			 gp_addr);

	/*
	 * KASAN is interested only in the non-canonical case, clear it
	 * otherwise.
	 */
	if (hint != GP_NON_CANONICAL)
		gp_addr = 0;

	die_addr(desc, regs, error_code, gp_addr);

exit:
	cond_local_irq_disable(regs);
}

static bool do_int3(struct pt_regs *regs)
{
	int res;


	res = notify_die(DIE_INT3, "int3", regs, 0, X86_TRAP_BP, SIGTRAP);

	return res == NOTIFY_STOP;
}
NOKPROBE_SYMBOL(do_int3);

static void do_int3_user(struct pt_regs *regs)
{
	if (do_int3(regs))
		return;

	cond_local_irq_enable(regs);
	do_trap(X86_TRAP_BP, SIGTRAP, "int3", regs, 0, 0, NULL);
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY_RAW(exc_int3)
{
	/*
	 * poke_int3_handler() is completely self contained code; it does (and
	 * must) *NOT* call out to anything, lest it hits upon yet another
	 * INT3.
	 */
	if (poke_int3_handler(regs))
		return;

	/*
	 * irqentry_enter_from_user_mode() uses static_branch_{,un}likely()
	 * and therefore can trigger INT3, hence poke_int3_handler() must
	 * be done before. If the entry came from kernel mode, then use
	 * nmi_enter() because the INT3 could have been hit in any context
	 * including NMI.
	 */
	if (user_mode(regs)) {
		irqentry_enter_from_user_mode(regs);
		instrumentation_begin();
		do_int3_user(regs);
		instrumentation_end();
		irqentry_exit_to_user_mode(regs);
	} else {
		irqentry_state_t irq_state = irqentry_nmi_enter(regs);

		instrumentation_begin();
		if (!do_int3(regs))
			die("int3", regs, 0);
		instrumentation_end();
		irqentry_nmi_exit(regs, irq_state);
	}
}


static bool is_sysenter_singlestep(struct pt_regs *regs)
{
	/*
	 * We don't try for precision here.  If we're anywhere in the region of
	 * code that can be single-stepped in the SYSENTER entry path, then
	 * assume that this is a useless single-step trap due to SYSENTER
	 * being invoked with TF set.  (We don't know in advance exactly
	 * which instructions will be hit because BTF could plausibly
	 * be set.)
	 */
	return (regs->ip - (unsigned long)__begin_SYSENTER_singlestep_region) <
		(unsigned long)__end_SYSENTER_singlestep_region -
		(unsigned long)__begin_SYSENTER_singlestep_region;
}

static __always_inline unsigned long debug_read_clear_dr6(void)
{
	unsigned long dr6;

	/*
	 * The Intel SDM says:
	 *
	 *   Certain debug exceptions may clear bits 0-3. The remaining
	 *   contents of the DR6 register are never cleared by the
	 *   processor. To avoid confusion in identifying debug
	 *   exceptions, debug handlers should clear the register before
	 *   returning to the interrupted task.
	 *
	 * Keep it simple: clear DR6 immediately.
	 */
	get_debugreg(dr6, 6);
	set_debugreg(DR6_RESERVED, 6);
	dr6 ^= DR6_RESERVED; /* Flip to positive polarity */

	return dr6;
}

/*
 * Our handling of the processor debug registers is non-trivial.
 * We do not clear them on entry and exit from the kernel. Therefore
 * it is possible to get a watchpoint trap here from inside the kernel.
 * However, the code in ./ptrace.c has ensured that the user can
 * only set watchpoints on userspace addresses. Therefore the in-kernel
 * watchpoint trap can only occur in code which is reading/writing
 * from user space. Such code must not hold kernel locks (since it
 * can equally take a page fault), therefore it is safe to call
 * force_sig_info even though that claims and releases locks.
 *
 * Code in ./signal.c ensures that the debug control register
 * is restored before we deliver any signal, and therefore that
 * user code runs with the correct debug control register even though
 * we clear it here.
 *
 * Being careful here means that we don't have to be as careful in a
 * lot of more complicated places (task switching can be a bit lazy
 * about restoring all the debug state, and ptrace doesn't have to
 * find every occurrence of the TF bit that could be saved away even
 * by user code)
 *
 * May run on IST stack.
 */

static bool notify_debug(struct pt_regs *regs, unsigned long *dr6)
{
	/*
	 * Notifiers will clear bits in @dr6 to indicate the event has been
	 * consumed - hw_breakpoint_handler(), single_stop_cont().
	 *
	 * Notifiers will set bits in @virtual_dr6 to indicate the desire
	 * for signals - ptrace_triggered(), kgdb_hw_overflow_handler().
	 */
	if (notify_die(DIE_DEBUG, "debug", regs, (long)dr6, 0, SIGTRAP) == NOTIFY_STOP)
		return true;

	return false;
}

static __always_inline void exc_debug_kernel(struct pt_regs *regs,
					     unsigned long dr6)
{
	/*
	 * Disable breakpoints during exception handling; recursive exceptions
	 * are exceedingly 'fun'.
	 *
	 * Since this function is NOKPROBE, and that also applies to
	 * HW_BREAKPOINT_X, we can't hit a breakpoint before this (XXX except a
	 * HW_BREAKPOINT_W on our stack)
	 *
	 * Entry text is excluded for HW_BP_X and cpu_entry_area, which
	 * includes the entry stack is excluded for everything.
	 */
	unsigned long dr7 = local_db_save();
	irqentry_state_t irq_state = irqentry_nmi_enter(regs);
	instrumentation_begin();

	/*
	 * If something gets miswired and we end up here for a user mode
	 * #DB, we will malfunction.
	 */
	WARN_ON_ONCE(user_mode(regs));

	if (test_thread_flag(TIF_BLOCKSTEP)) {
		/*
		 * The SDM says "The processor clears the BTF flag when it
		 * generates a debug exception." but PTRACE_BLOCKSTEP requested
		 * it for userspace, but we just took a kernel #DB, so re-set
		 * BTF.
		 */
		unsigned long debugctl;

		rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
		debugctl |= DEBUGCTLMSR_BTF;
		wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	}

	/*
	 * Catch SYSENTER with TF set and clear DR_STEP. If this hit a
	 * watchpoint at the same time then that will still be handled.
	 */
	if ((dr6 & DR_STEP) && is_sysenter_singlestep(regs))
		dr6 &= ~DR_STEP;

	/*
	 * The kernel doesn't use INT1
	 */
	if (!dr6)
		goto out;

	if (notify_debug(regs, &dr6))
		goto out;

	/*
	 * The kernel doesn't use TF single-step outside of:
	 *
	 *  - Kprobes, consumed through kprobe_debug_handler()
	 *  - KGDB, consumed through notify_debug()
	 *
	 * So if we get here with DR_STEP set, something is wonky.
	 *
	 * A known way to trigger this is through QEMU's GDB stub,
	 * which leaks #DB into the guest and causes IST recursion.
	 */
	if (WARN_ON_ONCE(dr6 & DR_STEP))
		regs->flags &= ~X86_EFLAGS_TF;
out:
	instrumentation_end();
	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(dr7);
}

static __always_inline void exc_debug_user(struct pt_regs *regs,
					   unsigned long dr6)
{
	bool icebp;

	/*
	 * If something gets miswired and we end up here for a kernel mode
	 * #DB, we will malfunction.
	 */
	WARN_ON_ONCE(!user_mode(regs));

	/*
	 * NB: We can't easily clear DR7 here because
	 * irqentry_exit_to_usermode() can invoke ptrace, schedule, access
	 * user memory, etc.  This means that a recursive #DB is possible.  If
	 * this happens, that #DB will hit exc_debug_kernel() and clear DR7.
	 * Since we're not on the IST stack right now, everything will be
	 * fine.
	 */

	irqentry_enter_from_user_mode(regs);
	instrumentation_begin();

	/*
	 * Start the virtual/ptrace DR6 value with just the DR_STEP mask
	 * of the real DR6. ptrace_triggered() will set the DR_TRAPn bits.
	 *
	 * Userspace expects DR_STEP to be visible in ptrace_get_debugreg(6)
	 * even if it is not the result of PTRACE_SINGLESTEP.
	 */
	current->thread.virtual_dr6 = (dr6 & DR_STEP);

	/*
	 * The SDM says "The processor clears the BTF flag when it
	 * generates a debug exception."  Clear TIF_BLOCKSTEP to keep
	 * TIF_BLOCKSTEP in sync with the hardware BTF flag.
	 */
	clear_thread_flag(TIF_BLOCKSTEP);

	/*
	 * If dr6 has no reason to give us about the origin of this trap,
	 * then it's very likely the result of an icebp/int01 trap.
	 * User wants a sigtrap for that.
	 */
	icebp = !dr6;

	if (notify_debug(regs, &dr6))
		goto out;

	/* It's safe to allow irq's after DR6 has been saved */
	local_irq_enable();

	if (v8086_mode(regs)) {
		handle_vm86_trap((struct kernel_vm86_regs *)regs, 0, X86_TRAP_DB);
		goto out_irq;
	}

	/* #DB for bus lock can only be triggered from userspace. */
	if (dr6 & DR_BUS_LOCK)
		handle_bus_lock(regs);

	/* Add the virtual_dr6 bits for signals. */
	dr6 |= current->thread.virtual_dr6;
	if (dr6 & (DR_STEP | DR_TRAP_BITS) || icebp)
		send_sigtrap(regs, 0, get_si_code(dr6));

out_irq:
	local_irq_disable();
out:
	instrumentation_end();
	irqentry_exit_to_user_mode(regs);
}

/* 32 bit does not have separate entry points. */
DEFINE_IDTENTRY_RAW(exc_debug)
{
	unsigned long dr6 = debug_read_clear_dr6();

	if (user_mode(regs))
		exc_debug_user(regs, dr6);
	else
		exc_debug_kernel(regs, dr6);
}

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
static void math_error(struct pt_regs *regs, int trapnr)
{
	struct task_struct *task = current;
	struct fpu *fpu = &task->thread.fpu;
	int si_code;
	char *str = (trapnr == X86_TRAP_MF) ? "fpu exception" :
						"simd exception";

	cond_local_irq_enable(regs);

	if (!user_mode(regs)) {
		if (fixup_exception(regs, trapnr, 0, 0))
			goto exit;

		task->thread.error_code = 0;
		task->thread.trap_nr = trapnr;

		if (notify_die(DIE_TRAP, str, regs, 0, trapnr,
			       SIGFPE) != NOTIFY_STOP)
			die(str, regs, 0);
		goto exit;
	}

	/*
	 * Synchronize the FPU register state to the memory register state
	 * if necessary. This allows the exception handler to inspect it.
	 */
	fpu_sync_fpstate(fpu);

	task->thread.trap_nr	= trapnr;
	task->thread.error_code = 0;

	si_code = fpu__exception_code(fpu, trapnr);
	/* Retry when we get spurious exceptions: */
	if (!si_code)
		goto exit;

	if (fixup_vdso_exception(regs, trapnr, 0, 0))
		goto exit;

	force_sig_fault(SIGFPE, si_code,
			(void __user *)uprobe_get_trap_addr(regs));
exit:
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY(exc_coprocessor_error)
{
	math_error(regs, X86_TRAP_MF);
}

DEFINE_IDTENTRY(exc_simd_coprocessor_error)
{
	if (IS_ENABLED(CONFIG_X86_INVD_BUG)) {
		/* AMD 486 bug: INVD in CPL 0 raises #XF instead of #GP */
		if (!static_cpu_has(X86_FEATURE_XMM)) {
			__exc_general_protection(regs, 0);
			return;
		}
	}
	math_error(regs, X86_TRAP_XF);
}

DEFINE_IDTENTRY(exc_spurious_interrupt_bug)
{
	/*
	 * This addresses a Pentium Pro Erratum:
	 *
	 * PROBLEM: If the APIC subsystem is configured in mixed mode with
	 * Virtual Wire mode implemented through the local APIC, an
	 * interrupt vector of 0Fh (Intel reserved encoding) may be
	 * generated by the local APIC (Int 15).  This vector may be
	 * generated upon receipt of a spurious interrupt (an interrupt
	 * which is removed before the system receives the INTA sequence)
	 * instead of the programmed 8259 spurious interrupt vector.
	 *
	 * IMPLICATION: The spurious interrupt vector programmed in the
	 * 8259 is normally handled by an operating system's spurious
	 * interrupt handler. However, a vector of 0Fh is unknown to some
	 * operating systems, which would crash if this erratum occurred.
	 *
	 * In theory this could be limited to 32bit, but the handler is not
	 * hurting and who knows which other CPUs suffer from this.
	 */
}

static bool handle_xfd_event(struct pt_regs *regs)
{
	u64 xfd_err;
	int err;

	if (!IS_ENABLED(CONFIG_X86_64) || !cpu_feature_enabled(X86_FEATURE_XFD))
		return false;

	rdmsrl(MSR_IA32_XFD_ERR, xfd_err);
	if (!xfd_err)
		return false;

	wrmsrl(MSR_IA32_XFD_ERR, 0);

	/* Die if that happens in kernel space */
	if (WARN_ON(!user_mode(regs)))
		return false;

	local_irq_enable();

	err = xfd_enable_feature(xfd_err);

	switch (err) {
	case -EPERM:
		force_sig_fault(SIGILL, ILL_ILLOPC, error_get_trap_addr(regs));
		break;
	case -EFAULT:
		force_sig(SIGSEGV);
		break;
	}

	local_irq_disable();
	return true;
}

DEFINE_IDTENTRY(exc_device_not_available)
{
	unsigned long cr0 = read_cr0();

	if (handle_xfd_event(regs))
		return;


	/* This should not happen. */
	if (WARN(cr0 & X86_CR0_TS, "CR0.TS was set")) {
		/* Try to fix it up and carry on. */
		write_cr0(cr0 & ~X86_CR0_TS);
	} else {
		/*
		 * Something terrible happened, and we're better off trying
		 * to kill the task than getting stuck in a never-ending
		 * loop of #NM faults.
		 */
		die("unexpected #NM exception", regs, 0);
	}
}


DEFINE_IDTENTRY_SW(iret_error)
{
	local_irq_enable();
	if (notify_die(DIE_TRAP, "iret exception", regs, 0,
			X86_TRAP_IRET, SIGILL) != NOTIFY_STOP) {
		do_trap(X86_TRAP_IRET, SIGILL, "iret exception", regs, 0,
			ILL_BADSTK, (void __user *)NULL);
	}
	local_irq_disable();
}

void __init trap_init(void)
{
	/* Init cpu_entry_area before IST entries are set up */
	setup_cpu_entry_areas();

	/* Init GHCB memory pages when running as an SEV-ES guest */
	sev_es_init_vc_handling();

	/* Initialize TSS before setting up traps so ISTs work */
	cpu_init_exception_handling();
	/* Setup traps as cpu_init() might #GP */
	idt_setup_traps();
	cpu_init();
}
