#include <linux/sched/debug.h>
#include <linux/sched/mm.h>
#include <linux/cpu.h>

#include <asm/mmu_context.h>

#include <asm/switch_to.h>
#include <linux/sched/task.h>

/*
 * One CPU, one run queue, round robin. Runnable tasks wait in FIFO order on
 * runqueues.queue; the task on the CPU is not queued. The idle task runs when
 * the queue is empty and is never queued itself. The tick ends a task's slice
 * after RR_TIMESLICE ticks when another task is waiting; the switch happens
 * at the next schedule() point (return to user mode, cond_resched()).
 *
 * Nothing in this kernel puts a task to sleep (there are no wait queues and
 * no code sets a sleeping task state), so a task only leaves the CPU when it
 * is preempted, and then goes straight back to the tail of the queue.
 */
#define RR_TIMESLICE (100 * HZ / 1000)

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

int sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	p->__state = TASK_NEW;
	p->slice_ticks = 0;
	return 0;
}

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
	if (!next->mm) {
		next->active_mm = prev->active_mm;
		if (prev->mm)
			mmgrab(prev->active_mm);
		else
			prev->active_mm = NULL;
	} else {
		switch_mm_irqs_off(prev->active_mm, next->mm, next);

		if (!prev->mm) {
			runqueues.prev_mm = prev->active_mm;
			prev->active_mm = NULL;
		}
	}

	switch_to(prev, next, prev);
	barrier();

	finish_task_switch();
}

void scheduler_tick(void)
{
	struct task_struct *curr;

	raw_spin_lock(&runqueues.__lock);
	curr = runqueues.curr;
	if (curr != runqueues.idle && ++curr->slice_ticks >= RR_TIMESLICE &&
	    !list_empty(&runqueues.queue))
		resched_curr();
	raw_spin_unlock(&runqueues.__lock);
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
		next->slice_ticks = 0;
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

void __sched schedule_preempt_disabled(void)
{
	sched_preempt_enable_no_resched();
	schedule();
	preempt_disable();
}

static void __sched notrace preempt_schedule_common(void)
{
	do {
		preempt_disable_notrace();
		__schedule(true);
		preempt_enable_no_resched_notrace();

	} while (need_resched());
}

int __sched __cond_resched(void)
{
	if (should_resched(0)) {
		preempt_schedule_common();
		return 1;
	}
	return 0;
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
