
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname)
#endif
#include <asm/kdebug.h>
int notify_die(enum die_val val, const char *str, struct pt_regs *regs,
	       long err, int trap, int sig);

#include <asm/traps.h>

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

static __always_inline int is_valid_bugaddr(unsigned long addr)
{
	if (addr < TASK_SIZE_MAX)
		return 0;
	return *(unsigned short *)addr == INSN_UD2;
}

DEFINE_IDTENTRY(exc_divide_error)
{
	die("divide error", regs, 0);
}

DEFINE_IDTENTRY(exc_overflow)
{
	die("overflow", regs, 0);
}

DEFINE_IDTENTRY_RAW(exc_invalid_op)
{
	irqentry_state_t state;

	if (!user_mode(regs) && is_valid_bugaddr(regs->ip)) {
		if (regs->flags & X86_EFLAGS_IF)
			raw_local_irq_enable();
		if (report_bug(regs->ip, regs) == BUG_TRAP_TYPE_WARN) {
			regs->ip += LEN_UD2;
			if (regs->flags & X86_EFLAGS_IF)
				raw_local_irq_disable();
			return;
		}
		if (regs->flags & X86_EFLAGS_IF)
			raw_local_irq_disable();
	}

	state = irqentry_enter(regs);
	die("invalid opcode", regs, 0);
	irqentry_exit(regs, state);
}

DEFINE_IDTENTRY(exc_coproc_segment_overrun)
{
	die("coprocessor segment overrun", regs, 0);
}

DEFINE_IDTENTRY_ERRORCODE(exc_invalid_tss)
{
	die("invalid TSS", regs, error_code);
}

DEFINE_IDTENTRY_ERRORCODE(exc_segment_not_present)
{
	die("segment not present", regs, error_code);
}

DEFINE_IDTENTRY_ERRORCODE(exc_stack_segment)
{
	die("stack segment", regs, error_code);
}

DEFINE_IDTENTRY_ERRORCODE(exc_alignment_check)
{
	die("alignment check", regs, error_code);
}

DEFINE_IDTENTRY_DF(exc_double_fault)
{
	irqentry_nmi_enter(regs);
	pr_emerg("PANIC: double fault, error_code: 0x%lx\n", error_code);
	die("double fault", regs, error_code);
	panic("Machine halted.");
}

DEFINE_IDTENTRY(exc_bounds)
{
	die("bounds", regs, 0);
}

#define GPFSTR "general protection fault"

DEFINE_IDTENTRY_ERRORCODE(exc_general_protection)
{
	cond_local_irq_enable(regs);

	if (fixup_exception(regs, X86_TRAP_GP, error_code, 0))
		goto exit;

	die(GPFSTR, regs, error_code);

exit:
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY_RAW(exc_int3)
{
	irqentry_state_t irq_state = irqentry_nmi_enter(regs);
	die("int3", regs, 0);
	irqentry_nmi_exit(regs, irq_state);
}

DEFINE_IDTENTRY_RAW(exc_debug)
{
	set_debugreg(DR6_RESERVED, 6);
}

DEFINE_IDTENTRY(exc_coprocessor_error)
{
	die("fpu exception", regs, 0);
}

DEFINE_IDTENTRY(exc_simd_coprocessor_error)
{
	die("simd exception", regs, 0);
}

DEFINE_IDTENTRY(exc_spurious_interrupt_bug)
{
}

DEFINE_IDTENTRY(exc_device_not_available)
{
	die("unexpected #NM exception", regs, 0);
}

DEFINE_IDTENTRY_SW(iret_error)
{
	die("iret exception", regs, 0);
}

void __init trap_init(void)
{
	setup_cpu_entry_areas();
	cpu_init_exception_handling();
	idt_setup_traps();
	cpu_init();
}
