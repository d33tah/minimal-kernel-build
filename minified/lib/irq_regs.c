/* IRQ register storage */
#include <asm/irq_regs.h>

DEFINE_PER_CPU(struct pt_regs *, __irq_regs);
