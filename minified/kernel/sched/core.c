// SPDX-License-Identifier: GPL-2.0-only
/*
 *  kernel/sched/core.c
 *
 *  Core kernel scheduler code and related syscalls
 *
 *  Copyright (C) 1991-2002  Linus Torvalds
 */
#include <linux/sched/debug.h>
#include <linux/sched/mm.h>
#include <linux/cpu.h>

#include <asm/mmu_context.h>

#include <asm/switch_to.h>
#include <linux/sched/task.h>

/*
 * One CPU, one run queue. Runnable tasks wait in FIFO order on
 * runqueues.queue; the task on the CPU is not queued. The idle task runs when
 * the queue is empty and is never queued itself. There is no time slicing:
 * fork allows one task besides idle (max_threads in kernel/fork.c), so the
 * only switch is idle -> init, when rest_init() schedules after creating it.
 *
 * Nothing in this kernel puts a task to sleep (there are no wait queues and
 * no code sets a sleeping task state), and init's exit panics, so init keeps
 * the CPU from then on.
 */

static struct rq {
	raw_spinlock_t __lock;
	struct list_head queue;
	struct task_struct *curr, *idle;
	struct mm_struct *prev_mm;
} runqueues = {
	.__lock = __RAW_SPIN_LOCK_UNLOCKED(runqueues.__lock),
	.queue = LIST_HEAD_INIT(runqueues.queue),
};

static void resched_curr(void)
{
	struct task_struct *curr = runqueues.curr;

	if (test_tsk_need_resched(curr))
		return;

	set_tsk_need_resched(curr);
	set_preempt_need_resched();
}

/*
 * fork()/clone()-time setup:
 */
int sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	/*
	 * We mark the process as NEW here. This guarantees that
	 * nobody will actually run it, and a signal or other external
	 * event cannot wake it up and insert it on the runqueue either.
	 */
	p->__state = TASK_NEW;
	return 0;
}

/*
 * wake_up_new_task - wake up a newly created task for the first time.
 *
 * This function will do some initial scheduler statistics housekeeping
 * that must be done for every newly created context, then puts the task
 * on the runqueue and wakes it.
 */
void wake_up_new_task(struct task_struct *p)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&runqueues.__lock, flags);
	WRITE_ONCE(p->__state, TASK_RUNNING);
	list_add_tail(&p->run_list, &runqueues.queue);
	if (runqueues.curr == runqueues.idle)
		resched_curr();
	raw_spin_unlock_irqrestore(&runqueues.__lock, flags);
}

static void finish_task_switch(void) __releases(runqueues.__lock)
{
	struct mm_struct *mm = runqueues.prev_mm;

	if (WARN_ONCE(preempt_count() != 2 * PREEMPT_DISABLE_OFFSET,
		      "corrupted preempt_count: %s/%d/0x%x\n", current->comm,
		      current->pid, preempt_count()))
		preempt_count_set(FORK_PREEMPT_COUNT);

	runqueues.prev_mm = NULL;
	raw_spin_unlock_irq(&runqueues.__lock);

	if (mm)
		mmdrop_sched(mm);
}

asmlinkage __visible void schedule_tail(struct task_struct *prev)
	__releases(runqueues.__lock)
{
	finish_task_switch();
	preempt_enable();
}

static __always_inline void context_switch(struct task_struct *prev,
					   struct task_struct *next)
{
	/*
	 * kernel -> kernel   lazy + transfer active
	 *   user -> kernel   lazy + mmgrab() active
	 *
	 * kernel ->   user   switch + mmdrop() active
	 *   user ->   user   switch
	 */
	if (!next->mm) { // to kernel
		next->active_mm = prev->active_mm;
		if (prev->mm) // from user
			mmgrab(prev->active_mm);
		else
			prev->active_mm = NULL;
	} else { // to user
		/*
		 * sys_membarrier() requires an smp_mb() between setting
		 * rq->curr / membarrier_switch_mm() and returning to userspace.
		 *
		 * The below provides this either through switch_mm(), or in
		 * case 'prev->active_mm == next->mm' through
		 * finish_task_switch()'s mmdrop().
		 */
		switch_mm_irqs_off(prev->active_mm, next->mm, next);

		if (!prev->mm) { // from kernel
			runqueues.prev_mm = prev->active_mm;
			prev->active_mm = NULL;
		}
	}

	/* Here we just switch the register state and the stack. */
	switch_to(prev, next, prev);
	barrier();

	finish_task_switch();
}

static void __sched notrace __schedule(bool preempt)
{
	struct task_struct *prev = runqueues.curr, *next;

	if (unlikely(in_atomic_preempt_off()))
		preempt_count_set(PREEMPT_DISABLED);

	local_irq_disable();

	raw_spin_lock(&runqueues.__lock);

	if (prev != runqueues.idle)
		list_add_tail(&prev->run_list, &runqueues.queue);
	if (list_empty(&runqueues.queue)) {
		next = runqueues.idle;
	} else {
		next = list_first_entry(&runqueues.queue, struct task_struct,
					run_list);
		list_del(&next->run_list);
	}
	clear_tsk_need_resched(prev);
	clear_preempt_need_resched();

	if (likely(prev != next)) {
		runqueues.curr = next;
		context_switch(prev, next);
	} else {
		raw_spin_unlock_irq(&runqueues.__lock);
	}
}

asmlinkage __visible void __sched schedule(void)
{
	do {
		preempt_disable();
		__schedule(false);
		sched_preempt_enable_no_resched();
	} while (need_resched());
}

/**
 * schedule_preempt_disabled - called with preemption disabled
 *
 * Returns with preemption disabled. Note: preempt_count must be 1
 */
void __sched schedule_preempt_disabled(void)
{
	sched_preempt_enable_no_resched();
	schedule();
	preempt_disable();
}

/* The idle task: halt until an interrupt makes another task runnable. */
void __noreturn cpu_startup_entry(enum cpuhp_state state)
{
	for (;;) {
		while (!need_resched()) {
			local_irq_disable();
			if (need_resched())
				local_irq_enable();
			else
				arch_cpu_idle(); /* sti; hlt */
		}
		__schedule(false);
	}
}

void __init sched_init(void)
{
	struct task_struct *idle = current;

	mmgrab(&init_mm);

	idle->__state = TASK_RUNNING;
	idle->flags |= PF_IDLE | PF_KTHREAD;
	runqueues.idle = idle;
	runqueues.curr = idle;
	init_idle_preempt_count(idle, smp_processor_id());
}
