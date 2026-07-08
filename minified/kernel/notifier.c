#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/notifier.h>

int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
			       unsigned long val, void *v)
{
	/*
	 * No notifier is ever registered on any atomic chain in this build
	 * (there is no notifier_chain_register / atomic_notifier_chain_register
	 * caller anywhere). The only call sites pass panic_notifier_list and
	 * vt_notifier_list, both ATOMIC_NOTIFIER_HEAD()-initialised and never
	 * populated, so the chain is permanently empty and walking it always
	 * yields NOTIFY_DONE. Return that directly.
	 */
	return NOTIFY_DONE;
}

int notrace notify_die(enum die_val val, const char *str,
	       struct pt_regs *regs, long err, int trap, int sig)
{
	/*
	 * No die-notifier is ever registered in this build (there is no
	 * register_die_notifier() caller anywhere), so the die_chain is
	 * permanently empty and atomic_notifier_call_chain() would always
	 * return NOTIFY_DONE (0). Return that directly. Every caller only
	 * tests for == NOTIFY_STOP, which can never happen here.
	 */
	return NOTIFY_DONE;
}
