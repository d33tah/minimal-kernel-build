// SPDX-License-Identifier: GPL-2.0
/*
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the lowest level x86-specific interrupt
 * entry, irq-stacks and irq statistics code. All the remaining
 * irq logic is done by the generic kernel/irq/ code and
 * by the x86-specific irq controller code. (e.g. i8259.c and
 * io_apic.c.)
 */

#include <linux/irq.h>
#include <linux/mm.h>

static DEFINE_PER_CPU(struct irq_stack *, hardirq_stack_ptr);

static inline void *current_stack(void)
{
	return (void *)(current_stack_pointer & ~(THREAD_SIZE - 1));
}

static inline int execute_on_irq_stack(struct irq_desc *desc)
{
	struct irq_stack *curstk, *irqstk;
	u32 *isp, *prev_esp, arg1;

	curstk = (struct irq_stack *)current_stack();
	irqstk = __this_cpu_read(hardirq_stack_ptr);

	/*
	 * this is where we switch to the IRQ stack. However, if we are
	 * already using the IRQ stack (because we interrupted a hardirq
	 * handler) we can't do that and just have to keep using the
	 * current stack (which is the irq stack already after all)
	 */
	if (unlikely(curstk == irqstk))
		return 0;

	isp = (u32 *)((char *)irqstk + sizeof(*irqstk));

	/* Save the next esp at the bottom of the stack */
	prev_esp = (u32 *)irqstk;
	*prev_esp = current_stack_pointer;

	asm volatile("xchgl	%%ebx,%%esp	\n" CALL_NOSPEC
		     "movl	%%ebx,%%esp	\n"
		     : "=a"(arg1), "=b"(isp)
		     : "0"(desc), "1"(isp), [thunk_target] "D"(desc->handle_irq)
		     : "memory", "cc", "ecx");
	return 1;
}

/*
 * Allocate per-cpu stacks for hardirq and softirq processing
 */
int irq_init_percpu_irqstack(unsigned int cpu)
{
	int node = cpu_to_node(cpu);
	struct page *ph;

	if (per_cpu(hardirq_stack_ptr, cpu))
		return 0;

	ph = alloc_pages_node(node, THREADINFO_GFP, THREAD_SIZE_ORDER);
	if (!ph)
		return -ENOMEM;

	per_cpu(hardirq_stack_ptr, cpu) = page_address(ph);
	return 0;
}

void __handle_irq(struct irq_desc *desc, struct pt_regs *regs)
{
	if (user_mode(regs) || !execute_on_irq_stack(desc)) {
		generic_handle_irq_desc(desc);
	}
}
