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

static inline
void init_irq_work(struct irq_work *work, void (*func)(struct irq_work *))
{
	*work = IRQ_WORK_INIT(func);
}

bool irq_work_queue(struct irq_work *work);
bool irq_work_queue_on(struct irq_work *work, int cpu);

void irq_work_tick(void);
void irq_work_sync(struct irq_work *work);

/* arch_irq_work_has_interrupt removed - unused */

void irq_work_run(void);
bool irq_work_needs_cpu(void);
void irq_work_single(void *arg);

#endif  
