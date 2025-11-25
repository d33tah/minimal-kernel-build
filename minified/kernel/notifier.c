 
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>

 
BLOCKING_NOTIFIER_HEAD(reboot_notifier_list);

 

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

static int notifier_chain_unregister(struct notifier_block **nl,
		struct notifier_block *n)
{
	while ((*nl) != NULL) {
		if ((*nl) == n) {
			rcu_assign_pointer(*nl, n->next);
			return 0;
		}
		nl = &((*nl)->next);
	}
	return -ENOENT;
}

 
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

 
static int notifier_call_chain_robust(struct notifier_block **nl,
				     unsigned long val_up, unsigned long val_down,
				     void *v)
{
	int ret, nr = 0;

	ret = notifier_call_chain(nl, val_up, v, -1, &nr);
	if (ret & NOTIFY_STOP_MASK)
		notifier_call_chain(nl, val_down, v, nr-1, NULL);

	return ret;
}

 

 
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

/* Stub: atomic_notifier_chain_register_unique_prio not used in minimal kernel */
int atomic_notifier_chain_register_unique_prio(struct atomic_notifier_head *nh,
					       struct notifier_block *n)
{
	return 0;
}

 
int atomic_notifier_chain_unregister(struct atomic_notifier_head *nh,
		struct notifier_block *n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&nh->lock, flags);
	ret = notifier_chain_unregister(&nh->head, n);
	spin_unlock_irqrestore(&nh->lock, flags);
	synchronize_rcu();
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

 
bool atomic_notifier_call_chain_is_empty(struct atomic_notifier_head *nh)
{
	return !rcu_access_pointer(nh->head);
}

 

static int __blocking_notifier_chain_register(struct blocking_notifier_head *nh,
					      struct notifier_block *n,
					      bool unique_priority)
{
	int ret;

	 
	if (unlikely(system_state == SYSTEM_BOOTING))
		return notifier_chain_register(&nh->head, n, unique_priority);

	down_write(&nh->rwsem);
	ret = notifier_chain_register(&nh->head, n, unique_priority);
	up_write(&nh->rwsem);
	return ret;
}

 
int blocking_notifier_chain_register(struct blocking_notifier_head *nh,
		struct notifier_block *n)
{
	return __blocking_notifier_chain_register(nh, n, false);
}

/* Stub: blocking_notifier_chain_register_unique_prio not used in minimal kernel */
int blocking_notifier_chain_register_unique_prio(struct blocking_notifier_head *nh,
						 struct notifier_block *n)
{
	return 0;
}

 
int blocking_notifier_chain_unregister(struct blocking_notifier_head *nh,
		struct notifier_block *n)
{
	int ret;

	 
	if (unlikely(system_state == SYSTEM_BOOTING))
		return notifier_chain_unregister(&nh->head, n);

	down_write(&nh->rwsem);
	ret = notifier_chain_unregister(&nh->head, n);
	up_write(&nh->rwsem);
	return ret;
}

/* Stub: blocking_notifier_call_chain_robust not used in minimal kernel */
int blocking_notifier_call_chain_robust(struct blocking_notifier_head *nh,
		unsigned long val_up, unsigned long val_down, void *v)
{
	return NOTIFY_DONE;
}

 
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

 

 
int raw_notifier_chain_register(struct raw_notifier_head *nh,
		struct notifier_block *n)
{
	return notifier_chain_register(&nh->head, n, false);
}

/* Stub: raw_notifier_chain_unregister not used in minimal kernel */
int raw_notifier_chain_unregister(struct raw_notifier_head *nh,
		struct notifier_block *n)
{
	return 0;
}

/* Stub: raw_notifier_call_chain_robust not used in minimal kernel */
int raw_notifier_call_chain_robust(struct raw_notifier_head *nh,
		unsigned long val_up, unsigned long val_down, void *v)
{
	return NOTIFY_DONE;
}

 
int raw_notifier_call_chain(struct raw_notifier_head *nh,
		unsigned long val, void *v)
{
	return notifier_call_chain(&nh->head, val, v, -1, NULL);
}

 

 
/* Stub: srcu notifier functions not used in minimal kernel */
int srcu_notifier_chain_register(struct srcu_notifier_head *nh,
		struct notifier_block *n) { return 0; }
int srcu_notifier_chain_unregister(struct srcu_notifier_head *nh,
		struct notifier_block *n) { return 0; }
int srcu_notifier_call_chain(struct srcu_notifier_head *nh,
		unsigned long val, void *v) { return NOTIFY_DONE; }
void srcu_init_notifier_head(struct srcu_notifier_head *nh) { }


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

/* Stub: die notifier functions not used in minimal kernel */
int register_die_notifier(struct notifier_block *nb) { return 0; }
int unregister_die_notifier(struct notifier_block *nb) { return 0; }
