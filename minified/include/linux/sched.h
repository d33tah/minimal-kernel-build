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
#define SCHED_FLAG_KEEP_PARAMS		0x10
/* End uapi/linux/sched.h */

#include <asm/current.h>

#include <linux/pid.h>
#include <linux/mutex.h>
/* plist.h, irqflags.h, nodemask.h removed - unused in sched.h */
/* linux/hrtimer.h removed - entire hrtimer subsystem dead */
#include <linux/rbtree.h>
/* seccomp.h removed - header is empty */
#include <linux/rcupdate.h>
#include <linux/refcount.h>
/* --- Inlined from linux/resource.h --- */
#include <linux/time.h>
struct rlimit {
	__kernel_ulong_t	rlim_cur;
	__kernel_ulong_t	rlim_max;
};
#define _STK_LIM	(8*1024*1024)
#define MLOCK_LIMIT	(8*1024*1024)
#define RLIMIT_CPU		0
#define RLIMIT_FSIZE		1
#define RLIMIT_DATA		2
#define RLIMIT_STACK		3
#define RLIMIT_CORE		4
#define RLIMIT_RSS		5
#define RLIMIT_NPROC		6
#define RLIMIT_NOFILE		7
#define RLIMIT_MEMLOCK		8
#define RLIMIT_AS		9
#define RLIMIT_LOCKS		10
#define RLIMIT_SIGPENDING	11
#define RLIMIT_MSGQUEUE		12
#define RLIMIT_NICE		13
#define RLIMIT_RTPRIO		14
#define RLIMIT_RTTIME		15
#define RLIM_NLIMITS		16
#define RLIM_INFINITY		(~0UL)
#define MQ_BYTES_MAX	819200
#define INIT_RLIMITS							\
{									\
	[RLIMIT_CPU]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_FSIZE]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_DATA]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_STACK]		= {       _STK_LIM,  RLIM_INFINITY },	\
	[RLIMIT_CORE]		= {              0,  RLIM_INFINITY },	\
	[RLIMIT_RSS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_NPROC]		= {              0,              0 },	\
	[RLIMIT_NOFILE]		= {   INR_OPEN_CUR,   INR_OPEN_MAX },	\
	[RLIMIT_MEMLOCK]	= {    MLOCK_LIMIT,    MLOCK_LIMIT },	\
	[RLIMIT_AS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_LOCKS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_SIGPENDING]	= { 		0,	       0 },	\
	[RLIMIT_MSGQUEUE]	= {   MQ_BYTES_MAX,   MQ_BYTES_MAX },	\
	[RLIMIT_NICE]		= { 0, 0 },				\
	[RLIMIT_RTPRIO]		= { 0, 0 },				\
	[RLIMIT_RTTIME]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
}
/* end resource.h */
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
/* Inlined from signal_types.h (guarded to avoid redefinition with signal.h) */
#ifndef _LINUX_SIGNAL_TYPES_INLINED
#define _LINUX_SIGNAL_TYPES_INLINED
#include <linux/list.h>
#include <asm/signal.h>
#include <asm-generic/siginfo.h>

typedef struct kernel_siginfo {
	__SIGINFO;
} kernel_siginfo_t;

struct sigpending {
	struct list_head list;
	sigset_t signal;
};

struct sigaction {
	__sighandler_t	sa_handler;
	unsigned long	sa_flags;
#ifdef __ARCH_HAS_SA_RESTORER
	__sigrestore_t sa_restorer;
#endif
	sigset_t	sa_mask;
};

struct k_sigaction {
	struct sigaction sa;
};

#define SA_IMMUTABLE		0x00800000
#endif /* _LINUX_SIGNAL_TYPES_INLINED */
/* syscall_user_dispatch removed - never configured in minimal kernel */
/* mm_types_task.h inlined */
#ifndef _LINUX_MM_TYPES_TASK_H
#define _LINUX_MM_TYPES_TASK_H


#include <linux/threads.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>

#include <asm/page.h>

/* Inlined from asm/tlbbatch.h */
struct arch_tlbflush_unmap_batch {
	struct cpumask cpumask;
};

/* NR_CPUS=1 < CONFIG_SPLIT_PTLOCK_CPUS=4, so USE_SPLIT_*=0 */
#define USE_SPLIT_PTE_PTLOCKS	0
#define USE_SPLIT_PMD_PTLOCKS	0
#define ALLOC_SPLIT_PTLOCKS	0

#define VMACACHE_BITS 2
#define VMACACHE_SIZE (1U << VMACACHE_BITS)
#define VMACACHE_MASK (VMACACHE_SIZE - 1)

struct vmacache {
	u64 seqnum;
	struct vm_area_struct *vmas[VMACACHE_SIZE];
};

/* MM_FILEPAGES/ANONPAGES/SWAPENTS/SHMEMPAGES enum, mm_rss_stat struct removed - write-only */

/* struct page_frag removed - never instantiated */

struct tlbflush_unmap_batch {
	 
	struct arch_tlbflush_unmap_batch arch;

	 
	bool flush_required;

	 
	bool writable;
};


#endif /* _LINUX_MM_TYPES_TASK_H */
/* struct task_io_accounting, posix-timers.h removed - empty structs no longer needed */
#include <linux/seqlock.h>
#define KM_MAX_IDX 16

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
#define TASK_PARKED			0x0040
#define TASK_DEAD			0x0080
#define TASK_WAKEKILL			0x0100
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

#define	MAX_SCHEDULE_TIMEOUT		LONG_MAX

extern long schedule_timeout(long timeout);
/* schedule_timeout_interruptible removed - never called */
extern long schedule_timeout_uninterruptible(long timeout);
asmlinkage void schedule(void);
extern void schedule_preempt_disabled(void);
/* preempt_schedule_irq removed - unused */
extern void io_schedule(void);

/* struct prev_cputime removed - write-only (only initialized, never read) */
/* SCHED_FIXEDPOINT_SHIFT removed - unused */

struct load_weight {
	unsigned long			weight;
	/* inv_weight removed - write-only (never read after __calc_delta simplification) */
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

/* sched_rt_entity removed - RT scheduler stubbed, struct was empty */
/* sched_dl_entity removed - DL scheduler stubbed, struct was empty */


/* union rcu_special removed - never used */
/* enum perf_event_task_context removed - PERF_EVENTS disabled */

/* wake_q_node removed - wake_q functions removed */

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
	/* ptrace field removed - always 0, ptrace fully stubbed */

	int				on_rq;

	int				prio;
	int				static_prio;
	int				normal_prio;
	unsigned int			rt_priority;

	struct sched_entity		se;
	/* rt/dl fields removed - structs were empty */
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
	 
	/* pdeath_signal removed - write-only, never read */

	unsigned long			jobctl;

	 
	unsigned int			personality;

	unsigned			sched_reset_on_fork:1;
	/* sched_contributes_to_load removed - write-only field */

	unsigned			:0;

	 

	 
	/* in_execve removed - write-only field */
	unsigned			in_iowait:1;
	unsigned			restore_sigmask:1;

	unsigned long			atomic_flags;

	/* restart_block removed - never accessed */

	pid_t				pid;
	pid_t				tgid;

	 

	 
	struct task_struct __rcu	*real_parent;
	/* parent, children, sibling removed - write-only in single-task kernel */
	struct task_struct		*group_leader;

	 
	/* ptraced, ptrace_entry removed - ptrace fully stubbed */

	 
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

	/* syscall_dispatch, parent_exec_id, self_exec_id removed */

	 
	spinlock_t			alloc_lock;

	 
	raw_spinlock_t			pi_lock;

	/* wake_q field removed - wake_q functions removed */








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

/* task_pid removed - inlined into pid.c */

pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type, struct pid_namespace *ns);

/* task_pid_nr inlined at fs/binfmt_elf.c - single caller (just tsk->pid) */
/* task_pid_nr_ns inlined into exec.c (~3 LOC) */
/* task_pid_vnr inlined into sched/core.c (~3 LOC) */

/* is_global_init removed - inlined into signal.c */


#define PF_IDLE			0x00000002
#define PF_EXITING		0x00000004
/* PF_POSTCOREDUMP removed - never used */
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
/* PF_RANDOMIZE removed - ASLR disabled, never set */
#define PF_NO_SETAFFINITY	0x04000000
#define PF_MEMALLOC_PIN		0x10000000


#define PFA_SPEC_SSB_DISABLE		3
#define PFA_SPEC_IB_DISABLE		5
#define PFA_SPEC_SSB_NOEXEC		7

#define TASK_PFA_TEST(name, func)					\
	static inline bool task_##func(struct task_struct *p)		\
	{ return test_bit(PFA_##name, &p->atomic_flags); }

#define TASK_PFA_CLEAR(name, func)					\
	static inline void task_clear_##func(struct task_struct *p)	\
	{ clear_bit(PFA_##name, &p->atomic_flags); }

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

extern int sched_setscheduler_nocheck(struct task_struct *, int, const struct sched_param *);
/* sched_set_fifo declaration removed - never called */

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

/* set_task_comm removed - inlined into kthread.c */

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

/* test_and_clear_tsk_thread_flag removed - inlined into process.c */

static inline int test_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_ti_thread_flag(task_thread_info(tsk), flag);
}

/* set_tsk_need_resched removed - inlined into sched/core.c */

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
