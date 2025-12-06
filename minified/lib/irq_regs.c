/* IRQ register storage */
#include <linux/percpu.h>
#include <asm/irq_regs.h>

#ifndef ARCH_HAS_OWN_IRQ_REGS
DEFINE_PER_CPU(struct pt_regs *, __irq_regs);
#endif
