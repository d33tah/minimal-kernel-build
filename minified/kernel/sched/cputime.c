/* Simplified CPU time accounting for minimal kernel
 * Hello World doesn't need accurate CPU time tracking
 */

#include <linux/sched.h>
#include <linux/kernel_stat.h>

#include "sched.h"

/* All account_* functions removed - empty stubs not called */

void thread_group_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st)
{
	*ut = 0;
	*st = 0;
}
