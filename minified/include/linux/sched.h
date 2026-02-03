#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

/* Inlined from uapi/linux/sched.h */
#include <linux/types.h>
#define CSIGNAL		0x000000ff
#define CLONE_VM	0x00000100
#define CLONE_FS	0x00000200
#define CLONE_FILES	0x00000400
#define CLONE_UNTRACED		0x00800000
/* Removed unused CLONE_* flags: CLONE_SIGHAND, CLONE_PIDFD, CLONE_PTRACE,
   CLONE_VFORK, CLONE_PARENT, CLONE_THREAD, CLONE_SETTLS, CLONE_PARENT_SETTID,
   CLONE_CHILD_CLEARTID, CLONE_CHILD_SETTID, CLONE_NEWUSER, CLONE_CLEAR_SIGHAND */
#define SCHED_NORMAL		0
#define SCHED_FIFO		1
#define SCHED_RR		2
#define SCHED_BATCH		3
#define SCHED_IDLE		5
#define SCHED_DEADLINE		6
#define SCHED_RESET_ON_FORK     0x40000000
#define SCHED_FLAG_RESET_ON_FORK	0x01
#define SCHED_FLAG_KEEP_PARAMS		0x10
/* End uapi/linux/sched.h */

#include <asm/current.h>

#include <linux/pid.h>
#include <linux/mutex.h>
/* plist.h, irqflags.h, nodemask.h removed - unused in sched.h */
#include <linux/hrtimer.h>
/* seccomp.h removed - header is empty */
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/resource.h>
#define MAX_NICE	19
#define MIN_NICE	-20
#define NICE_WIDTH	(MAX_NICE - MIN_NICE + 1)
#define MAX_RT_PRIO		100
#define MAX_PRIO		(MAX_RT_PRIO + NICE_WIDTH)
#define DEFAULT_PRIO		(MAX_RT_PRIO + NICE_WIDTH / 2)
#define NICE_TO_PRIO(nice)	((nice) + DEFAULT_PRIO)
#define PRIO_TO_NICE(prio)	((prio) - DEFAULT_PRIO)
/* end sched/prio.h */
#include <linux/sched/types.h>
#include <linux/signal_types.h>
/* syscall_user_dispatch.h inlined */
struct syscall_user_dispatch { char __user *selector; unsigned long offset; unsigned long len; bool on_dispatch; };
#include <linux/mm_types_task.h>
/* struct task_io_accounting, posix-timers.h removed - empty structs no longer needed */
#include <linux/seqlock.h>
#include <asm/kmap_size.h>

/* Unused forward declarations removed: cfs_rq, mempolicy, pipe_inode_info, rq, sched_attr, seq_file */
struct fs_struct;
struct nameidata;
struct nsproxy;
struct pid_namespace;
struct sched_param;
struct sighand_struct;
struct signal_struct;


#define TASK_RUNNING			0x0000
#define TASK_INTERRUPTIBLE		0x0001
#define TASK_UNINTERRUPTIBLE		0x0002
/* __TASK_STOPPED, __TASK_TRACED removed - unused */
#define EXIT_DEAD			0x0010
#define EXIT_ZOMBIE			0x0020
#define TASK_PARKED			0x0040
#define TASK_DEAD			0x0080
#define TASK_WAKEKILL			0x0100
#define TASK_WAKING			0x0200
/* TASK_NOLOAD removed - never used */
#define TASK_NEW			0x0800

#define TASK_KILLABLE			(TASK_WAKEKILL | TASK_UNINTERRUPTIBLE)

#define TASK_NORMAL			(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)

/* task_is_running removed - unused */
/* debug_normal/special_state_change removed - empty stubs */

#define __set_current_state(state_value)				\
	WRITE_ONCE(current->__state, (state_value))

#define set_current_state(state_value)					\
	smp_store_mb(current->__state, (state_value))

#define set_special_state(state_value)					\
	do {								\
		unsigned long flags;  			\
									\
		raw_spin_lock_irqsave(&current->pi_lock, flags);	\
		WRITE_ONCE(current->__state, (state_value));		\
		raw_spin_unlock_irqrestore(&current->pi_lock, flags);	\
	} while (0)

enum {
	TASK_COMM_LEN = 16,
};

extern void scheduler_tick(void);

#define	MAX_SCHEDULE_TIMEOUT		LONG_MAX

extern long schedule_timeout(long timeout);
/* schedule_timeout_interruptible removed - never called */
extern long schedule_timeout_uninterruptible(long timeout);
asmlinkage void schedule(void);
extern void schedule_preempt_disabled(void);
asmlinkage void preempt_schedule_irq(void);

extern void io_schedule(void);

/* struct prev_cputime removed - write-only (only initialized, never read) */

# define SCHED_FIXEDPOINT_SHIFT		10

struct load_weight {
	unsigned long			weight;
	u32				inv_weight;
};

/* struct sched_statistics removed - schedstat_enabled() always 0 */

struct sched_entity {
	struct load_weight		load;
	struct rb_node			run_node;
	/* group_node removed - write-only, only initialized */
	unsigned int			on_rq;

	u64				exec_start;
	u64				sum_exec_runtime;
	u64				vruntime;
	/* prev_sum_exec_runtime removed - write-only */
};

struct sched_rt_entity {
	/* run_list, timeout, watchdog_stamp, time_slice, on_rq, on_list, back removed - write-only/unused */
} __randomize_layout;

struct sched_dl_entity {
	/* rb_node removed - deadline scheduler stubbed, never used */
};


/* union rcu_special removed - never used */
/* enum perf_event_task_context removed - PERF_EVENTS disabled */

struct wake_q_node {
	struct wake_q_node *next;
};

struct kmap_ctrl {
	int				idx;
	pte_t				pteval[KM_MAX_IDX];
};

struct task_struct {
	 
	struct thread_info		thread_info;
	unsigned int			__state;


	 
	randomized_struct_fields_start

	void				*stack;
	refcount_t			usage;
	 
	unsigned int			flags;
	unsigned int			ptrace;

	int				on_rq;

	int				prio;
	int				static_prio;
	int				normal_prio;
	unsigned int			rt_priority;

	struct sched_entity		se;
	struct sched_rt_entity		rt;
	struct sched_dl_entity		dl;
	const struct sched_class	*sched_class;
	/* stats field removed - schedstat_enabled() always 0 */

	unsigned int			policy;
	/* nr_cpus_allowed removed - only written, never read */
	const cpumask_t			*cpus_ptr;
	cpumask_t			cpus_mask;

	struct list_head		tasks;

	struct mm_struct		*mm;
	struct mm_struct		*active_mm;


	struct vmacache			vmacache;

	int				exit_state;
	/* exit_code removed - write-only field (never read) */
	int				exit_signal;
	 
	int				pdeath_signal;
	 
	unsigned long			jobctl;

	 
	unsigned int			personality;

	unsigned			sched_reset_on_fork:1;
	/* sched_contributes_to_load removed - write-only field */

	unsigned			:0;

	 

	 
	unsigned			sched_remote_wakeup:1;
	/* in_execve removed - write-only field */
	unsigned			in_iowait:1;
	unsigned			restore_sigmask:1;

	unsigned long			atomic_flags;  

	struct restart_block		restart_block;

	pid_t				pid;
	pid_t				tgid;

	 

	 
	struct task_struct __rcu	*real_parent;

	 
	struct task_struct __rcu	*parent;

	 
	struct list_head		children;
	struct list_head		sibling;
	struct task_struct		*group_leader;

	 
	struct list_head		ptraced;
	struct list_head		ptrace_entry;

	 
	struct pid			*thread_pid;
	struct hlist_node		pid_links[PIDTYPE_MAX];
	struct list_head		thread_group;
	struct list_head		thread_node;

	/* vfork_done removed - write-only field (CLONE_VFORK never used) */


	int __user			*set_child_tid;

	/* clear_child_tid removed - write-only (CLONE_CHILD_CLEARTID never set) */


	void				*worker_private;
	/* utime, stime removed - write-only, task_cputime() never called */

	/* nvcsw, nivcsw removed - write-only fields (accumulation removed) */
	/* min_flt, maj_flt removed - write-only fields (increments removed) */
	/* start_time, posix_cputimers removed - write-only fields */
	/* ptracer_cred removed - write-only (passed to empty stubs, never read) */

	const struct cred __rcu		*real_cred;

	 
	const struct cred __rcu		*cred;


	 
	char				comm[TASK_COMM_LEN];

	struct nameidata		*nameidata;

	 
	struct fs_struct		*fs;

	 
	struct files_struct		*files;


	 
	struct nsproxy			*nsproxy;

	 
	struct signal_struct		*signal;
	struct sighand_struct __rcu		*sighand;
	sigset_t			blocked;
	/* real_blocked removed - never accessed */
	sigset_t			saved_sigmask;
	struct sigpending		pending;
	/* sas_ss_sp, sas_ss_size, sas_ss_flags removed - write-only fields */

	struct callback_head		*task_works;

	struct syscall_user_dispatch	syscall_dispatch;

	/* parent_exec_id, self_exec_id removed - write-only fields */

	 
	spinlock_t			alloc_lock;

	 
	raw_spinlock_t			pi_lock;

	struct wake_q_node		wake_q;








	/* plug, backing_dev_info, ioac (empty struct) removed - write-only, never read */
	/* perf_event_* fields removed - PERF_EVENTS disabled, never used */

	union {
		refcount_t		rcu_users;
		struct rcu_head		rcu;
	};

	/* splice_pipe, task_frag removed - only set to NULL, never used */

	/* nr_dirtied, timer_slack_ns removed - initialized but never read */




	struct kmap_ctrl		kmap_ctrl;
	int				pagefault_disabled;

	refcount_t			stack_refcount;

	/* l1d_flush_kill removed - never used */

	randomized_struct_fields_end

	 
	struct thread_struct		thread;

	 
};

static inline struct pid *task_pid(struct task_struct *task)
{
	return task->thread_pid;
}

pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type, struct pid_namespace *ns);

/* task_pid_nr inlined at fs/binfmt_elf.c - single caller (just tsk->pid) */
/* task_pid_nr_ns inlined into exec.c (~3 LOC) */
/* task_pid_vnr inlined into sched/core.c (~3 LOC) */

static inline int is_global_init(struct task_struct *tsk)
{
	return tsk->tgid == 1;
}


#define PF_IDLE			0x00000002
#define PF_EXITING		0x00000004
/* PF_POSTCOREDUMP removed - never used */
#define PF_IO_WORKER		0x00000010
#define PF_WQ_WORKER		0x00000020
#define PF_FORKNOEXEC		0x00000040
#define PF_SUPERPRIV		0x00000100
#define PF_MEMALLOC		0x00000800
/* PF_NPROC_EXCEEDED removed - never used */
#define PF_NOFREEZE		0x00008000
/* PF_FROZEN removed - unused */
#define PF_MEMALLOC_NOFS	0x00040000
#define PF_MEMALLOC_NOIO	0x00080000
#define PF_KTHREAD		0x00200000
#define PF_RANDOMIZE		0x00400000
#define PF_NO_SETAFFINITY	0x04000000
#define PF_MEMALLOC_PIN		0x10000000

#define PFA_NO_NEW_PRIVS		0
#define PFA_SPEC_SSB_DISABLE		3
#define PFA_SPEC_IB_DISABLE		5
#define PFA_SPEC_SSB_NOEXEC		7

#define TASK_PFA_TEST(name, func)					\
	static inline bool task_##func(struct task_struct *p)		\
	{ return test_bit(PFA_##name, &p->atomic_flags); }

#define TASK_PFA_CLEAR(name, func)					\
	static inline void task_clear_##func(struct task_struct *p)	\
	{ clear_bit(PFA_##name, &p->atomic_flags); }

TASK_PFA_TEST(NO_NEW_PRIVS, no_new_privs)

TASK_PFA_TEST(SPEC_SSB_DISABLE, spec_ssb_disable)
TASK_PFA_CLEAR(SPEC_SSB_DISABLE, spec_ssb_disable)

TASK_PFA_TEST(SPEC_SSB_NOEXEC, spec_ssb_noexec)
TASK_PFA_CLEAR(SPEC_SSB_NOEXEC, spec_ssb_noexec)

TASK_PFA_TEST(SPEC_IB_DISABLE, spec_ib_disable)

static inline void
current_restore_flags(unsigned long orig_flags, unsigned long flags)
{
	current->flags &= ~flags;
	current->flags |= orig_flags & flags;
}

/* do_set_cpus_allowed removed - empty stub, call site removed */
static inline int set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask)
{
	if (!cpumask_test_cpu(0, new_mask))
		return -EINVAL;
	return 0;
}

/* task_nice inlined into sched/core.c (~3 LOC) */

extern int sched_setscheduler(struct task_struct *, int, const struct sched_param *);
extern int sched_setscheduler_nocheck(struct task_struct *, int, const struct sched_param *);
extern void sched_set_fifo(struct task_struct *p);

/* is_idle_task inlined at kernel/rcu/tiny.c - single caller */


union thread_union {
	struct task_struct task;
	unsigned long stack[THREAD_SIZE/sizeof(long)];
};


extern unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)];

# define task_thread_info(task)	(&(task)->thread_info)


extern struct task_struct *find_task_by_pid_ns(pid_t nr, struct pid_namespace *ns);

extern int wake_up_state(struct task_struct *tsk, unsigned int state);
extern int wake_up_process(struct task_struct *tsk);
extern void wake_up_new_task(struct task_struct *tsk);
/* kick_process removed - empty stub, no callers */

extern void __set_task_comm(struct task_struct *tsk, const char *from, bool exec);

static inline void set_task_comm(struct task_struct *tsk, const char *from)
{
	__set_task_comm(tsk, from, false);
}

static inline void set_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	set_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	clear_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline int test_and_set_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_and_set_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline int test_and_clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_and_clear_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline int test_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void set_tsk_need_resched(struct task_struct *tsk)
{
	set_tsk_thread_flag(tsk,TIF_NEED_RESCHED);
}

static inline void clear_tsk_need_resched(struct task_struct *tsk)
{
	clear_tsk_thread_flag(tsk,TIF_NEED_RESCHED);
}

static inline int test_tsk_need_resched(struct task_struct *tsk)
{
	return unlikely(test_tsk_thread_flag(tsk,TIF_NEED_RESCHED));
}

extern int __cond_resched(void);

#define cond_resched() __cond_resched()


static __always_inline bool need_resched(void)
{
	return unlikely(tif_need_resched());
}


static inline unsigned int task_cpu(const struct task_struct *p)
{
	return 0;
}

/* sched_setaffinity extern removed - function replaced with COND_SYSCALL
   rseq_execve, rseq_handle_notify_resume, rseq_signal_deliver, rseq_preempt, rseq_migrate,
   rseq_fork, rseq_syscall, sched_core_free, sched_core_fork removed - unused */

#endif
