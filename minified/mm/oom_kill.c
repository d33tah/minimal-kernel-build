/* Minimal includes for OOM stubs */
#include <linux/oom.h>
#include <linux/mutex.h>
#include <linux/notifier.h>

DEFINE_MUTEX(oom_lock);
DEFINE_MUTEX(oom_adj_mutex);

bool __oom_reap_task_mm(struct mm_struct *mm)
{
	return true;
}

/* find_lock_task_mm removed - unused */
/* process_shares_mm removed - unused */

void exit_oom_victim(void)
{
	 
}

void oom_killer_enable(void)
{
	 
}

/* oom_killer_disable removed - never called */

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
