/* Minimal OOM stubs */
#include <linux/oom.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
bool __oom_reap_task_mm(struct mm_struct *mm)
{
	return true;
}
/* out_of_memory removed - was never called */
