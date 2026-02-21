#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <linux/completion.h>

/* inlined from linux/rcupdate_wait.h */
struct rcu_synchronize {
	struct rcu_head head;
	struct completion completion;
};
void wakeme_after_rcu(struct rcu_head *head);

void __wait_rcu_gp(bool checktiny, int n, call_rcu_func_t *crcu_array,
		   struct rcu_synchronize *rs_array);

#define _wait_rcu_gp(checktiny, ...)                                         \
	do {                                                                 \
		call_rcu_func_t __crcu_array[] = { __VA_ARGS__ };            \
		struct rcu_synchronize __rs_array[ARRAY_SIZE(__crcu_array)]; \
		__wait_rcu_gp(checktiny, ARRAY_SIZE(__crcu_array),           \
			      __crcu_array, __rs_array);                     \
	} while (0)

#define wait_rcu_gp(...) _wait_rcu_gp(false, __VA_ARGS__)

/* rcu.h inlined */
extern void resched_cpu(int cpu);

struct rcu_ctrlblk {
	struct rcu_head *rcucblist;
	struct rcu_head **donetail;
	struct rcu_head **curtail;
	unsigned long gp_seq;
};

static struct rcu_ctrlblk rcu_ctrlblk = {
	.donetail = &rcu_ctrlblk.rcucblist,
	.curtail = &rcu_ctrlblk.rcucblist,
	.gp_seq = 0 - 300UL,
};

void rcu_barrier(void)
{
	wait_rcu_gp(call_rcu);
}

void rcu_qs(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (rcu_ctrlblk.donetail != rcu_ctrlblk.curtail) {
		rcu_ctrlblk.donetail = rcu_ctrlblk.curtail;
		raise_softirq_irqoff(RCU_SOFTIRQ);
	}
	WRITE_ONCE(rcu_ctrlblk.gp_seq, rcu_ctrlblk.gp_seq + 1);
	local_irq_restore(flags);
}

static __latent_entropy void
rcu_process_callbacks(struct softirq_action *unused)
{
	struct rcu_head *next, *list;
	unsigned long flags;

	local_irq_save(flags);
	if (rcu_ctrlblk.donetail == &rcu_ctrlblk.rcucblist) {
		local_irq_restore(flags);
		return;
	}
	list = rcu_ctrlblk.rcucblist;
	rcu_ctrlblk.rcucblist = *rcu_ctrlblk.donetail;
	*rcu_ctrlblk.donetail = NULL;
	if (rcu_ctrlblk.curtail == rcu_ctrlblk.donetail)
		rcu_ctrlblk.curtail = &rcu_ctrlblk.rcucblist;
	rcu_ctrlblk.donetail = &rcu_ctrlblk.rcucblist;
	local_irq_restore(flags);

	while (list) {
		next = list->next;
		prefetch(next);
		local_bh_disable();
		{
			unsigned long offset = (unsigned long)list->func;
			if (__is_kvfree_rcu_offset(offset)) {
				kvfree((void *)list - offset);
			} else {
				rcu_callback_t f = list->func;
				WRITE_ONCE(list->func, (rcu_callback_t)0L);
				f(list);
			}
		}
		local_bh_enable();
		list = next;
	}
}

void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	unsigned long flags;

	head->func = func;
	head->next = NULL;

	local_irq_save(flags);
	*rcu_ctrlblk.curtail = head;
	rcu_ctrlblk.curtail = &head->next;
	local_irq_restore(flags);

	if (unlikely(current->flags & PF_IDLE)) {
		resched_cpu(0);
	}
}

void __init rcu_init(void)
{
	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
}
