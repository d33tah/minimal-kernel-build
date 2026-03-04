#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname)
#endif
#include <asm/kdebug.h>
#include <asm/traps.h>

DECLARE_BITMAP(system_vectors, NR_VECTORS);

DEFINE_IDTENTRY(exc_divide_error)
{
	die("divide error", regs, 0);
}

DEFINE_IDTENTRY_RAW(exc_invalid_op)
{
	die("invalid opcode", regs, 0);
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

DEFINE_IDTENTRY_ERRORCODE(exc_general_protection)
{
	if (fixup_exception(regs, X86_TRAP_GP, error_code, 0))
		return;
	die("general protection fault", regs, error_code);
}

DEFINE_IDTENTRY_RAW(exc_int3)
{
	irqentry_state_t irq_state = irqentry_nmi_enter(regs);
	die("int3", regs, 0);
	irqentry_nmi_exit(regs, irq_state);
}

DEFINE_IDTENTRY_RAW(exc_nmi)
{
	irqentry_state_t irq_state;

	irq_state = irqentry_nmi_enter(regs);
	irqentry_nmi_exit(regs, irq_state);
}

DEFINE_IDTENTRY_RAW(exc_debug)
{
	set_debugreg(DR6_RESERVED, 6);
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
