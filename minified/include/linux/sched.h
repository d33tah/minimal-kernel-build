/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#define CSIGNAL 0x000000ff
#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_UNTRACED 0x00800000

#include <linux/pid.h>
#include <linux/smp.h>

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/resource.h>

struct fs_struct;
struct nsproxy;
struct signal_struct;

/*
 * Task state bitmask. NOTE! These bits are also
 * encoded in fs/proc/array.c: get_task_state().
 *
 * We have two separate sets of flags: task->state
 * is about runnability, while task->exit_state are
 * about the task exiting. Confusing, but this way
 * modifying one set can't modify the other one by
 * mistake.
 */

/* Used in tsk->state: */
#define TASK_RUNNING 0x0000
#define TASK_DEAD 0x0080
#define TASK_NEW 0x0800

/*
 * set_current_state() includes a barrier so that the write of current->state
 * is correctly serialised wrt the caller's subsequent test of whether to
 * actually sleep:
 *
 *   for (;;) {
 *	set_current_state(TASK_UNINTERRUPTIBLE);
 *	if (CONDITION)
 *	   break;
 *
 *	schedule();
 *   }
 *   __set_current_state(TASK_RUNNING);
 *
 * If the caller does not need such serialisation (because, for instance, the
 * CONDITION test and condition change and wakeup are under the same lock) then
 * use __set_current_state().
 *
 * The above is typically ordered against the wakeup, which does:
 *
 *   CONDITION = 1;
 *   wake_up_state(p, TASK_UNINTERRUPTIBLE);
 *
 * where wake_up_state()/try_to_wake_up() executes a full memory barrier before
 * accessing p->state.
 *
 * Wakeup will do: if (@state & p->state) p->state = TASK_RUNNING, that is,
 * once it observes the TASK_UNINTERRUPTIBLE store the waking CPU can issue a
 * TASK_RUNNING store which can collide with __set_current_state(TASK_RUNNING).
 *
 * However, with slightly different timing the wakeup TASK_RUNNING store can
 * also collide with the TASK_UNINTERRUPTIBLE store. Losing that store is not
 * a problem either because that will result in one extra go around the loop
 * and our @cond test will save the day.
 *
 * Also see the comments of try_to_wake_up().
 */
#define __set_current_state(state_value) \
	WRITE_ONCE(current->__state, (state_value))

/*
 * Define the task command name length as enum, then it can be visible to
 * BPF programs.
 */
enum {
	TASK_COMM_LEN = 16,
};

asmlinkage void schedule(void);
extern void schedule_preempt_disabled(void);

struct task_struct {
	struct thread_info thread_info;
	unsigned int __state;
	randomized_struct_fields_start void *stack;
	refcount_t usage;
	/* Per task flags (PF_*), defined further below: */
	unsigned int flags;
	struct list_head run_list;
	struct mm_struct *mm, *active_mm;

	/* Used for emulating ABI behavior of previous Linux versions: */
	unsigned int personality;
	pid_t pid;
	struct task_struct *group_leader;

	/* PID/PID hash table linkage. */
	struct pid *thread_pid;

	/* Objective and real subjective task credentials (COW): */
	const struct cred __rcu *real_cred;

	/* Effective (overridable) subjective task credentials (COW): */
	const struct cred __rcu *cred;

	/*
	 * executable name, excluding path.
	 *
	 * - normally initialized setup_new_exec()
	 * - access it with [gs]et_task_comm()
	 * - lock it with task_lock()
	 */
	char comm[TASK_COMM_LEN];
	/* Filesystem information: */
	struct fs_struct *fs;

	/* Open file information: */
	struct files_struct *files;

	/* Namespaces: */
	struct nsproxy *nsproxy;

	/* Signal handlers: */
	struct signal_struct *signal;

	/* Protection against (de-)allocation: mm, files, fs, tty, keyrings, mems_allowed, mempolicy: */
	spinlock_t alloc_lock;

	/* Protection of the PI data structures: */
	raw_spinlock_t pi_lock;
	int pagefault_disabled;
	randomized_struct_fields_end struct thread_struct thread;

	/*
	 * WARNING: on x86, 'thread_struct' contains a variable-sized
	 * structure.  It *MUST* be at the end of 'task_struct'.
	 *
	 * Do not put anything below here!
	 */
};

static inline struct pid *task_pid(struct task_struct *task)
{
	return task->thread_pid;
}

#define PF_IDLE 0x00000002 /* I am an IDLE thread */
#define PF_MEMALLOC_NOFS 0x00040000
#define PF_MEMALLOC_NOIO 0x00080000
#define PF_KTHREAD 0x00200000 /* I am a kernel thread */

union thread_union {
	unsigned long stack[THREAD_SIZE / sizeof(long)];
};

extern unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)];

#define task_thread_info(task) (&(task)->thread_info)

extern void wake_up_new_task(struct task_struct *tsk);

/*
 * Set thread flags in other task's structures.
 * See asm/thread_info.h for TIF_xxxx flags available:
 */
static inline void set_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	set_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	clear_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline int test_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void set_tsk_need_resched(struct task_struct *tsk)
{
	set_tsk_thread_flag(tsk, TIF_NEED_RESCHED);
}

static inline void clear_tsk_need_resched(struct task_struct *tsk)
{
	clear_tsk_thread_flag(tsk, TIF_NEED_RESCHED);
}

static inline int test_tsk_need_resched(struct task_struct *tsk)
{
	return unlikely(test_tsk_thread_flag(tsk, TIF_NEED_RESCHED));
}

static __always_inline bool need_resched(void)
{
	return unlikely(tif_need_resched());
}

#endif
