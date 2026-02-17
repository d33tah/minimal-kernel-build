
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/string.h>

void __kmap_local_sched_out(void)
{
}
void __kmap_local_sched_in(void)
{
}

void kmap_local_fork(struct task_struct *tsk)
{
	if (WARN_ON_ONCE(tsk->kmap_ctrl.idx))
		memset(&tsk->kmap_ctrl, 0, sizeof(tsk->kmap_ctrl));
}
