#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/kernel.h>
#include <linux/cpumask.h>

/* Inlined from irqreturn.h */
enum irqreturn {
	IRQ_NONE = (0 << 0),
	IRQ_HANDLED = (1 << 0),
};
typedef enum irqreturn irqreturn_t;

#include <linux/hardirq.h>
#include <linux/kref.h>
#include <linux/workqueue.h>

#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/sections.h>

typedef irqreturn_t (*irq_handler_t)(int, void *);

struct irqaction {
	irq_handler_t handler;
	void *dev_id;
};

extern irqreturn_t no_action(int cpl, void *dev_id);

extern int __must_check request_irq(unsigned int irq, irq_handler_t handler,
				    void *dev_id);

#ifndef local_softirq_pending

#ifndef local_softirq_pending_ref
#define local_softirq_pending_ref irq_stat.__softirq_pending
#endif

#define local_softirq_pending() (__this_cpu_read(local_softirq_pending_ref))
#define set_softirq_pending(x) \
	(__this_cpu_write(local_softirq_pending_ref, (x)))
#define or_softirq_pending(x) (__this_cpu_or(local_softirq_pending_ref, (x)))

#endif

enum {
	HI_SOFTIRQ = 0,
	TIMER_SOFTIRQ,
	NET_TX_SOFTIRQ,
	NET_RX_SOFTIRQ,
	BLOCK_SOFTIRQ,
	IRQ_POLL_SOFTIRQ,
	TASKLET_SOFTIRQ,
	SCHED_SOFTIRQ,
	HRTIMER_SOFTIRQ,
	RCU_SOFTIRQ,

	NR_SOFTIRQS
};

struct softirq_action {
	void (*action)(struct softirq_action *);
};

asmlinkage void __do_softirq(void);

extern void open_softirq(int nr, void (*action)(struct softirq_action *));
extern void softirq_init(void);

extern void raise_softirq_irqoff(unsigned int nr);

extern void early_irq_init(void);

#define __softirq_entry __section(".softirqentry.text")

#endif
