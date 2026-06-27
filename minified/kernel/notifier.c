#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>

static int notifier_call_chain(struct notifier_block **nl,
			       unsigned long val, void *v,
			       int nr_to_call, int *nr_calls)
{
	int ret = NOTIFY_DONE;
	struct notifier_block *nb, *next_nb;

	nb = rcu_dereference_raw(*nl);

	while (nb && nr_to_call) {
		next_nb = rcu_dereference_raw(nb->next);

		ret = nb->notifier_call(nb, val, v);

		if (nr_calls)
			(*nr_calls)++;

		if (ret & NOTIFY_STOP_MASK)
			break;
		nb = next_nb;
		nr_to_call--;
	}
	return ret;
}
NOKPROBE_SYMBOL(notifier_call_chain);

int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
			       unsigned long val, void *v)
{
	int ret;

	rcu_read_lock();
	ret = notifier_call_chain(&nh->head, val, v, -1, NULL);
	rcu_read_unlock();

	return ret;
}
NOKPROBE_SYMBOL(atomic_notifier_call_chain);

static ATOMIC_NOTIFIER_HEAD(die_chain);

int notrace notify_die(enum die_val val, const char *str,
	       struct pt_regs *regs, long err, int trap, int sig)
{
	struct die_args args = {
		.regs	= regs,
		.str	= str,
		.err	= err,
		.trapnr	= trap,
		.signr	= sig,

	};
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			   "notify_die called but RCU thinks we're quiescent");
	return atomic_notifier_call_chain(&die_chain, val, &args);
}
NOKPROBE_SYMBOL(notify_die);
