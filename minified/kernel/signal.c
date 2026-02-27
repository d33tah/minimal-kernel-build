#include <linux/sched/signal.h>

void force_sig(int sig)
{
	panic("force_sig");
}
void __set_current_blocked(const sigset_t *newset)
{
}
