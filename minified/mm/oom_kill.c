
#include <linux/oom.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/mutex.h>

DEFINE_MUTEX(oom_lock);
DEFINE_MUTEX(oom_adj_mutex);

bool __oom_reap_task_mm(struct mm_struct *mm)
{
	return true;
}

struct task_struct *find_lock_task_mm(struct task_struct *p)
{
	return NULL;
}

bool process_shares_mm(struct task_struct *p, struct mm_struct *mm)
{
	return false;
}

void exit_oom_victim(void)
{
	 
}

void oom_killer_enable(void)
{
	 
}

bool oom_killer_disable(signed long timeout)
{
	return true;
}

int register_oom_notifier(struct notifier_block *nb)
{
	return 0;
}

int unregister_oom_notifier(struct notifier_block *nb)
{
	return 0;
}

bool out_of_memory(struct oom_control *oc)
{
	return false;
}

void pagefault_out_of_memory(void)
{
	 
}
