#include <linux/sched/signal.h>

void force_sig(int sig)
{
	panic("force_sig");
}
int force_sig_fault(int sig, int code, void __user *addr)
{
	panic("force_sig_fault");
}

void __set_current_blocked(const sigset_t *newset)
{
}
