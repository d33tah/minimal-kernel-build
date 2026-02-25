#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/kernel.h>

enum irqreturn {
	IRQ_NONE		= (0 << 0),
};
typedef enum irqreturn irqreturn_t;

#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/kref.h>
#include <linux/jump_label.h>

#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/sections.h>

#define IRQF_TRIGGER_MASK	0x0000000f

#define IRQF_NO_THREAD		0x00010000
#define IRQF_NO_AUTOEN		0x00080000

typedef irqreturn_t (*irq_handler_t)(int, void *);

struct irqaction {
	irq_handler_t		handler;
	void			*dev_id;
	struct irqaction	*next;
	unsigned int		irq;
	unsigned int		flags;
	const char		*name;
} ____cacheline_internodealigned_in_smp;

extern irqreturn_t no_action(int cpl, void *dev_id);

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

#ifndef local_softirq_pending

#ifndef local_softirq_pending_ref
#define local_softirq_pending_ref irq_stat.__softirq_pending
#endif

#define local_softirq_pending()	(__this_cpu_read(local_softirq_pending_ref))
#define set_softirq_pending(x)	(__this_cpu_write(local_softirq_pending_ref, (x)))
#define or_softirq_pending(x)	(__this_cpu_or(local_softirq_pending_ref, (x)))

#endif  

enum
{
	RCU_SOFTIRQ = 0,

	NR_SOFTIRQS
};

struct softirq_action
{
	void	(*action)(struct softirq_action *);
};

asmlinkage void do_softirq(void);
asmlinkage void __do_softirq(void);

extern void open_softirq(int nr, void (*action)(struct softirq_action *));

extern void raise_softirq_irqoff(unsigned int nr);

extern int early_irq_init(void);

#ifndef __irq_entry
# define __irq_entry	 __section(".irqentry.text")
#endif

#define __softirq_entry  __section(".softirqentry.text")

#endif
