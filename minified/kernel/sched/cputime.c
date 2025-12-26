/* Simplified CPU time accounting for minimal kernel
 * Hello World doesn't need accurate CPU time tracking
 */

#include <linux/sched.h>
#include <linux/kernel_stat.h>

#include "sched.h"

void account_user_time(struct task_struct *p, u64 cputime)
{
}

void account_guest_time(struct task_struct *p, u64 cputime)
{
}

void account_system_index_time(struct task_struct *p, u64 cputime,
			       enum cpu_usage_stat index)
{
}

void account_system_time(struct task_struct *p, int hardirq_offset, u64 cputime)
{
}

void account_idle_time(u64 cputime)
{
}

void account_process_tick(struct task_struct *p, int user_tick)
{
}

void thread_group_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st)
{
	*ut = 0;
	*st = 0;
}
