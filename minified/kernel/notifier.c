#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>

static int notifier_chain_register(struct notifier_block **nl,
				   struct notifier_block *n,
				   bool unique_priority)
{
	while ((*nl) != NULL) {
		if (unlikely((*nl) == n)) {
			WARN(1, "notifier callback %ps already registered",
			     n->notifier_call);
			return -EEXIST;
		}
		if (n->priority > (*nl)->priority)
			break;
		if (n->priority == (*nl)->priority && unique_priority)
			return -EBUSY;
		nl = &((*nl)->next);
	}
	n->next = *nl;
	rcu_assign_pointer(*nl, n);
	return 0;
}

static int notifier_call_chain(struct notifier_block **nl, unsigned long val,
			       void *v, int nr_to_call, int *nr_calls)
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

int atomic_notifier_chain_register(struct atomic_notifier_head *nh,
				   struct notifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = notifier_chain_register(&nh->head, n, false);
	spin_unlock_irqrestore(&nh->lock, flags);
	return ret;
}

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

/* atomic_notifier_call_chain_is_empty removed - never called */

/* __blocking_notifier_chain_register removed - only caller was blocking_notifier_chain_register */
/* blocking_notifier_chain_register removed - never called */

int blocking_notifier_call_chain(struct blocking_notifier_head *nh,
				 unsigned long val, void *v)
{
	int ret = NOTIFY_DONE;

	if (rcu_access_pointer(nh->head)) {
		down_read(&nh->rwsem);
		ret = notifier_call_chain(&nh->head, val, v, -1, NULL);
		up_read(&nh->rwsem);
	}
	return ret;
}

/* raw_notifier_call_chain removed - no callers */

static ATOMIC_NOTIFIER_HEAD(die_chain);

int notrace notify_die(enum die_val val, const char *str, struct pt_regs *regs,
		       long err, int trap, int sig)
{
	struct die_args args = {
		.regs = regs,
		.str = str,
		.err = err,
		.trapnr = trap,
		.signr = sig,

	};
	return atomic_notifier_call_chain(&die_chain, val, &args);
}
NOKPROBE_SYMBOL(notify_die);
