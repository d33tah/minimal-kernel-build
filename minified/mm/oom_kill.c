/* Minimal includes for OOM stubs */
#include <linux/oom.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
bool __oom_reap_task_mm(struct mm_struct *mm)
{
	return true;
}
void exit_oom_victim(void)
{
}
bool out_of_memory(struct oom_control *oc)
{
	return false;
}
void pagefault_out_of_memory(void)
{
}
