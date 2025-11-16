 
#ifndef _ASM_X86_IRQ_STACK_H
#define _ASM_X86_IRQ_STACK_H

#include <linux/ptrace.h>
#include <linux/objtool.h>

#include <asm/processor.h>

 
#define run_sysvec_on_irqstack_cond(func, regs)				\
{									\
	irq_enter_rcu();						\
	func(regs);							\
	irq_exit_rcu();							\
}

 
#define run_irq_on_irqstack_cond(func, regs, vector)			\
{									\
	irq_enter_rcu();						\
	func(regs, vector);						\
	irq_exit_rcu();							\
}


#endif
