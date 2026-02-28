#ifndef __LINUX_PREEMPT_H
#define __LINUX_PREEMPT_H


#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8
#define HARDIRQ_BITS	4
#define NMI_BITS	4

#define PREEMPT_SHIFT	0
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)
#define NMI_SHIFT	(HARDIRQ_SHIFT + HARDIRQ_BITS)

#define __IRQ_MASK(x)	((1UL << (x))-1)

#define SOFTIRQ_MASK	(__IRQ_MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)
#define HARDIRQ_MASK	(__IRQ_MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)
#define NMI_MASK	(__IRQ_MASK(NMI_BITS)     << NMI_SHIFT)

#define PREEMPT_OFFSET	(1UL << PREEMPT_SHIFT)
#define SOFTIRQ_OFFSET	(1UL << SOFTIRQ_SHIFT)
#define HARDIRQ_OFFSET	(1UL << HARDIRQ_SHIFT)
#define NMI_OFFSET	(1UL << NMI_SHIFT)

#define SOFTIRQ_DISABLE_OFFSET	(2 * SOFTIRQ_OFFSET)

#define PREEMPT_DISABLED	(PREEMPT_DISABLE_OFFSET + PREEMPT_ENABLED)

#define INIT_PREEMPT_COUNT	PREEMPT_OFFSET

/* asm/preempt.h inlined */
#include <asm/rmwcc.h>
#include <asm/percpu.h>
#include <linux/thread_info.h>
DECLARE_PER_CPU(int, __preempt_count);
#define PREEMPT_NEED_RESCHED	0x80000000
#define PREEMPT_ENABLED	(0 + PREEMPT_NEED_RESCHED)
static __always_inline int preempt_count(void)
{
	return raw_cpu_read_4(__preempt_count) & ~PREEMPT_NEED_RESCHED;
}
static __always_inline void preempt_count_set(int pc)
{
	int old, new;
	do {
		old = raw_cpu_read_4(__preempt_count);
		new = (old & PREEMPT_NEED_RESCHED) |
			(pc & ~PREEMPT_NEED_RESCHED);
	} while (raw_cpu_cmpxchg_4(__preempt_count, old, new) != old);
}
#define init_idle_preempt_count(p, cpu) do { \
	per_cpu(__preempt_count, (cpu)) = PREEMPT_DISABLED; \
} while (0)
static __always_inline void set_preempt_need_resched(void)
{
	raw_cpu_and_4(__preempt_count, ~PREEMPT_NEED_RESCHED);
}
static __always_inline void clear_preempt_need_resched(void)
{
	raw_cpu_or_4(__preempt_count, PREEMPT_NEED_RESCHED);
}
static __always_inline void __preempt_count_add(int val)
{
	raw_cpu_add_4(__preempt_count, val);
}
static __always_inline void __preempt_count_sub(int val)
{
	raw_cpu_add_4(__preempt_count, -val);
}
static __always_inline bool should_resched(int preempt_offset)
{
	return unlikely(raw_cpu_read_4(__preempt_count) == preempt_offset);
}

#define nmi_count()	(preempt_count() & NMI_MASK)
#define hardirq_count()	(preempt_count() & HARDIRQ_MASK)
# define softirq_count()	(preempt_count() & SOFTIRQ_MASK)
#define irq_count()	(nmi_count() | hardirq_count() | softirq_count())

#define in_nmi()		(nmi_count())
#define in_interrupt()		(irq_count())

# define PREEMPT_DISABLE_OFFSET	0

#define in_atomic()	(preempt_count() != 0)

#define in_atomic_preempt_off() (preempt_count() != PREEMPT_DISABLE_OFFSET)

#define preempt_count_add(val)	__preempt_count_add(val)
#define preempt_count_sub(val)	__preempt_count_sub(val)

#define preempt_count_dec() preempt_count_sub(1)

#define preempt_disable()			barrier()
#define sched_preempt_enable_no_resched()	barrier()
#define preempt_enable_no_resched()		barrier()
#define preempt_enable()			barrier()
#define preempt_disable_notrace()		barrier()
#define preempt_enable_no_resched_notrace()	barrier()
#define preempt_enable_notrace()		barrier()
#define preemptible()				0

#endif  
