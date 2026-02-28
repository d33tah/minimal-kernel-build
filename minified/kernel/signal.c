#include <linux/sched/signal.h>

void force_sig(int sig)
{
	panic("force_sig");
}
