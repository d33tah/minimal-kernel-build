#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

/* Inlined from uapi/linux/sched.h */
#define CSIGNAL 0x000000ff
#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_UNTRACED 0x00800000
/* End uapi/linux/sched.h */

#include <linux/pid.h>
#include <linux/smp.h>

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/resource.h>

struct fs_struct;
struct nsproxy;
struct signal_struct;

#define TASK_RUNNING 0x0000
#define TASK_DEAD 0x0080
#define TASK_NEW 0x0800

#define __set_current_state(state_value) \
	WRITE_ONCE(current->__state, (state_value))

enum {
	TASK_COMM_LEN = 16,
};

extern void scheduler_tick(void);

asmlinkage void schedule(void);
extern void schedule_preempt_disabled(void);

struct task_struct {
	struct thread_info thread_info;
	unsigned int __state;
	randomized_struct_fields_start void *stack;
	refcount_t usage;
	unsigned int flags;
	struct list_head run_list;
	unsigned int slice_ticks;
	struct mm_struct *mm, *active_mm;
	unsigned int personality;
	pid_t pid;
	struct task_struct *group_leader;
	struct pid *thread_pid;
	const struct cred __rcu *real_cred;
	const struct cred __rcu *cred;
	char comm[TASK_COMM_LEN];
	struct fs_struct *fs;
	struct files_struct *files;
	struct nsproxy *nsproxy;
	struct signal_struct *signal;
	spinlock_t alloc_lock;
	raw_spinlock_t pi_lock;
	int pagefault_disabled;
	randomized_struct_fields_end struct thread_struct thread;
};

static inline struct pid *task_pid(struct task_struct *task)
{
	return task->thread_pid;
}

#define PF_IDLE 0x00000002
#define PF_MEMALLOC_NOFS 0x00040000
#define PF_MEMALLOC_NOIO 0x00080000
#define PF_KTHREAD 0x00200000

union thread_union {
	unsigned long stack[THREAD_SIZE / sizeof(long)];
};

extern unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)];

#define task_thread_info(task) (&(task)->thread_info)

extern void wake_up_new_task(struct task_struct *tsk);

static inline void set_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	set_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	clear_ti_thread_flag(task_thread_info(tsk), flag);
}

/* test_and_set_tsk_thread_flag: orphaned with set_notify_resume, removed (LOC reduction) */

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

extern int __cond_resched(void);

static inline int _cond_resched(void)
{
	return __cond_resched();
}

#define cond_resched() ({ _cond_resched(); })

static __always_inline bool need_resched(void)
{
	return unlikely(tif_need_resched());
}

#endif
