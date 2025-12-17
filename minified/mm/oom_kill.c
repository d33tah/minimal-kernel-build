/* Minimal includes for OOM stubs */
#include <linux/oom.h>
#include <linux/mutex.h>
#include <linux/notifier.h>

/* oom_lock, oom_adj_mutex removed - never used */

bool __oom_reap_task_mm(struct mm_struct *mm)
{
	return true;
}

/* find_lock_task_mm removed - unused */
/* process_shares_mm removed - unused */

void exit_oom_victim(void)
{
	 
}

/* oom_killer_enable removed - never called (~4 LOC) */

/* oom_killer_disable removed - never called */

/* register_oom_notifier, unregister_oom_notifier removed - never called (~10 LOC) */

bool out_of_memory(struct oom_control *oc)
{
	return false;
}

void pagefault_out_of_memory(void)
{
	 
}
