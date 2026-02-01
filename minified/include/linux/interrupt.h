#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>

/* Inlined from irqreturn.h */
enum irqreturn {
	IRQ_NONE		= (0 << 0),
	IRQ_HANDLED		= (1 << 0),
	IRQ_WAKE_THREAD		= (1 << 1),
};
typedef enum irqreturn irqreturn_t;

#include <linux/irqnr.h>
#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/hrtimer.h>
#include <linux/kref.h>
#include <linux/workqueue.h>
#include <linux/jump_label.h>

#include <linux/atomic.h>
#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/sections.h>

#define IRQF_TRIGGER_MASK	0x0000000f

#define IRQF_SHARED		0x00000080
#define IRQF_PROBE_SHARED	0x00000100
/* __IRQF_TIMER removed - unused */
#define IRQF_PERCPU		0x00000400
#define IRQF_NOBALANCING	0x00000800
/* IRQF_IRQPOLL removed - unused */
#define IRQF_ONESHOT		0x00002000
#define IRQF_NO_SUSPEND		0x00004000
#define IRQF_NO_THREAD		0x00010000
#define IRQF_COND_SUSPEND	0x00040000
#define IRQF_NO_AUTOEN		0x00080000
#define IRQF_NO_DEBUG		0x00100000

/* IRQF_TIMER removed - unused */

/* IRQC_IS_HARDIRQ, IRQC_IS_NESTED enum removed - unused */

typedef irqreturn_t (*irq_handler_t)(int, void *);

struct irqaction {
	irq_handler_t		handler;
	void			*dev_id;
	struct irqaction	*next;
	irq_handler_t		thread_fn;
	struct task_struct	*thread;
	struct irqaction	*secondary;
	unsigned int		irq;
	unsigned int		flags;
	unsigned long		thread_flags;
	unsigned long		thread_mask;
	const char		*name;
} ____cacheline_internodealigned_in_smp;

extern irqreturn_t no_action(int cpl, void *dev_id);

#define IRQ_NOTCONNECTED	(1U << 31)

extern int __must_check
request_threaded_irq(unsigned int irq, irq_handler_t handler,
		     irq_handler_t thread_fn,
		     unsigned long flags, const char *name, void *dev);

static inline int __must_check
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, NULL, flags, name, dev);
}

/* free_irq, disable_irq_nosync, enable_irq removed - never called */
/* struct device forward decl removed - unused */
/* struct irq_affinity_desc, irq_set_affinity, irq_can_set_affinity removed - never used */

/* irqchip_irq_state enum removed - unused */
/* force_irqthreads_key and force_irqthreads() removed - never called */

#ifndef local_softirq_pending

#ifndef local_softirq_pending_ref
#define local_softirq_pending_ref irq_stat.__softirq_pending
#endif

#define local_softirq_pending()	(__this_cpu_read(local_softirq_pending_ref))
#define set_softirq_pending(x)	(__this_cpu_write(local_softirq_pending_ref, (x)))
#define or_softirq_pending(x)	(__this_cpu_or(local_softirq_pending_ref, (x)))

#endif  

/* hard_irq_disable removed - never called */


enum
{
	HI_SOFTIRQ=0,
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

struct softirq_action
{
	void	(*action)(struct softirq_action *);
};

asmlinkage void do_softirq(void);
asmlinkage void __do_softirq(void);

extern void open_softirq(int nr, void (*action)(struct softirq_action *));
extern void softirq_init(void);
extern void __raise_softirq_irqoff(unsigned int nr);

extern void raise_softirq_irqoff(unsigned int nr);
/* raise_softirq removed - never called */

DECLARE_PER_CPU(struct task_struct *, ksoftirqd);



/* init_irq_proc removed - unused */

extern int early_irq_init(void);
/* arch_probe_nr_irqs, arch_early_irq_init removed - inlined at call sites */

#ifndef __irq_entry
# define __irq_entry	 __section(".irqentry.text")
#endif

#define __softirq_entry  __section(".softirqentry.text")

#endif
