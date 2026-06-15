#ifndef _LINUX_IRQ_WORK_H
#define _LINUX_IRQ_WORK_H

#include <linux/smp_types.h>
#include <linux/rcuwait.h>


struct irq_work {
	struct __call_single_node node;
	void (*func)(struct irq_work *);
	struct rcuwait irqwait;
};

#define __IRQ_WORK_INIT(_func, _flags) (struct irq_work){	\
	.node = { .u_flags = (_flags), },			\
	.func = (_func),					\
	.irqwait = __RCUWAIT_INITIALIZER(irqwait),		\
}

#define IRQ_WORK_INIT(_func) __IRQ_WORK_INIT(_func, 0)

#define DEFINE_IRQ_WORK(name, _f)				\
	struct irq_work name = IRQ_WORK_INIT(_f)

void irq_work_tick(void);

#endif  
