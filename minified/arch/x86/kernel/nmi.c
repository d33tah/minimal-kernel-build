
#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname)
#endif
#include <linux/hardirq.h>
#include <asm/traps.h>
#include <asm/nmi.h>

DEFINE_IDTENTRY_RAW(exc_nmi)
{
	irqentry_state_t irq_state;

	irq_state = irqentry_nmi_enter(regs);
	irqentry_nmi_exit(regs, irq_state);
}

void local_touch_nmi(void)
{
}
