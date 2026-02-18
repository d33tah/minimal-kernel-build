
#include <linux/interrupt.h>
void do_softirq_own_stack(void);

#ifndef __ARCH_IRQ_STAT
DEFINE_PER_CPU_ALIGNED(irq_cpustat_t, irq_stat);
#endif

static struct softirq_action softirq_vec[NR_SOFTIRQS] __cacheline_aligned_in_smp;

void __local_bh_enable_ip(unsigned long ip, unsigned int cnt)
{
	WARN_ON_ONCE(in_hardirq());
	__preempt_count_sub(cnt - 1);

	if (unlikely(!in_interrupt() && local_softirq_pending())) {
		do_softirq();
	}

	preempt_count_dec();
}

asmlinkage __visible void do_softirq(void)
{
	__u32 pending;
	unsigned long flags;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	pending = local_softirq_pending();

	if (pending)
		do_softirq_own_stack();

	local_irq_restore(flags);
}

#define MAX_SOFTIRQ_RESTART 10

asmlinkage __visible void __softirq_entry __do_softirq(void)
{
	unsigned long old_flags = current->flags;
	int max_restart = MAX_SOFTIRQ_RESTART;
	struct softirq_action *h;
	__u32 pending;
	int softirq_bit;

	current->flags &= ~PF_MEMALLOC;

	pending = local_softirq_pending();

	__local_bh_disable_ip(_RET_IP_, SOFTIRQ_OFFSET);

restart:

	set_softirq_pending(0);

	local_irq_enable();

	h = softirq_vec;

	while ((softirq_bit = ffs(pending))) {
		int prev_count;

		h += softirq_bit - 1;

		prev_count = preempt_count();
		h->action(h);

		if (unlikely(prev_count != preempt_count()))
			preempt_count_set(prev_count);
		h++;
		pending >>= softirq_bit;
	}

	local_irq_disable();

	pending = local_softirq_pending();
	if (pending) {
		if (!need_resched() && --max_restart)
			goto restart;
	}

	__preempt_count_sub(SOFTIRQ_OFFSET);
	WARN_ON_ONCE(in_interrupt());
	current_restore_flags(old_flags, PF_MEMALLOC);
}

void irq_enter_rcu(void)
{
	__irq_enter_raw();
}

static inline void __irq_exit_rcu(void)
{
	local_irq_disable();
	preempt_count_sub(HARDIRQ_OFFSET);
	if (!in_interrupt() && local_softirq_pending()) {
		do_softirq_own_stack();
	}
}

void irq_exit_rcu(void)
{
	__irq_exit_rcu();
	/* lockdep_hardirq_exit is empty do{}while(0) */
}

inline void raise_softirq_irqoff(unsigned int nr)
{
	or_softirq_pending(1UL << nr);
}

void open_softirq(int nr, void (*action)(struct softirq_action *))
{
	softirq_vec[nr].action = action;
}
