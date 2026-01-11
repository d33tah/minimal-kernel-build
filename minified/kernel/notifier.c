#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>

/* notifier_chain_register, notifier_call_chain, atomic_notifier_chain_register removed - nothing ever registers */

int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
			       unsigned long val, void *v)
{
	return NOTIFY_DONE; /* No registrations, chain is always empty */
}
NOKPROBE_SYMBOL(atomic_notifier_call_chain);

/* atomic_notifier_call_chain_is_empty, blocking_notifier*, raw_notifier_call_chain removed - no callers */
/* die_chain removed - no registrations, notify_die always returns NOTIFY_DONE */

int notrace notify_die(enum die_val val, const char *str, struct pt_regs *regs,
		       long err, int trap, int sig)
{
	return NOTIFY_DONE;
}
NOKPROBE_SYMBOL(notify_die);
