/* Minimal OOM stubs - exit_oom_victim/pagefault_out_of_memory calls removed */
#include <linux/oom.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
bool __oom_reap_task_mm(struct mm_struct *mm)
{
	return true;
}
bool out_of_memory(struct oom_control *oc)
{
	return false;
}
