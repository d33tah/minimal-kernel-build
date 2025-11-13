// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stubbed OOM killer - minimal kernel doesn't need OOM management
 */

#include <linux/oom.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/mutex.h>

/* Required mutexes */
DEFINE_MUTEX(oom_lock);
DEFINE_MUTEX(oom_adj_mutex);

/* Stub: __oom_reap_task_mm */
bool __oom_reap_task_mm(struct mm_struct *mm)
{
	return true;
}

/* Stub: find_lock_task_mm */
struct task_struct *find_lock_task_mm(struct task_struct *p)
{
	return NULL;
}

/* Stub: process_shares_mm */
bool process_shares_mm(struct task_struct *p, struct mm_struct *mm)
{
	return false;
}

/* Stub: exit_oom_victim */
void exit_oom_victim(void)
{
	/* No-op */
}

/* Stub: oom_killer_enable */
void oom_killer_enable(void)
{
	/* No-op */
}

/* Stub: oom_killer_disable */
bool oom_killer_disable(signed long timeout)
{
	return true;
}

/* Stub: register_oom_notifier */
int register_oom_notifier(struct notifier_block *nb)
{
	return 0;
}

/* Stub: unregister_oom_notifier */
int unregister_oom_notifier(struct notifier_block *nb)
{
	return 0;
}

/* Stub: out_of_memory */
bool out_of_memory(struct oom_control *oc)
{
	return false;
}

/* Stub: pagefault_out_of_memory */
void pagefault_out_of_memory(void)
{
	/* No-op */
}
