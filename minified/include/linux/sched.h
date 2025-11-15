 
#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

 

#include <uapi/linux/sched.h>

#include <asm/current.h>

#include <linux/pid.h>
#include <linux/sem.h>
#include <linux/shm.h>
#include <linux/mutex.h>
#include <linux/plist.h>
#include <linux/hrtimer.h>
#include <linux/irqflags.h>
#include <linux/seccomp.h>
#include <linux/nodemask.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/resource.h>
#include <linux/latencytop.h>
#include <linux/sched/prio.h>
#include <linux/sched/types.h>
#include <linux/signal_types.h>
#include <linux/syscall_user_dispatch.h>
#include <linux/mm_types_task.h>
#include <linux/task_io_accounting.h>
#include <linux/posix-timers.h>
#include <linux/rseq.h>
#include <linux/seqlock.h>
#include <asm/kmap_size.h>

 
struct audit_context;
struct backing_dev_info;
struct bio_list;
struct blk_plug;
struct bpf_local_storage;
struct bpf_run_ctx;
struct capture_control;
struct cfs_rq;
struct fs_struct;
struct futex_pi_state;
struct io_context;
struct io_uring_task;
struct mempolicy;
struct nameidata;
struct nsproxy;
struct perf_event_context;
struct pid_namespace;
struct pipe_inode_info;
struct rcu_node;
struct reclaim_state;
struct robust_list_head;
struct root_domain;
struct rq;
struct sched_attr;
struct sched_param;
struct seq_file;
struct sighand_struct;
struct signal_struct;
struct task_delay_info;
struct task_group;

 

 
#define TASK_RUNNING			0x0000
#define TASK_INTERRUPTIBLE		0x0001
#define TASK_UNINTERRUPTIBLE		0x0002
#define __TASK_STOPPED			0x0004
#define __TASK_TRACED			0x0008
 
#define EXIT_DEAD			0x0010
#define EXIT_ZOMBIE			0x0020
#define EXIT_TRACE			(EXIT_ZOMBIE | EXIT_DEAD)
 
#define TASK_PARKED			0x0040
#define TASK_DEAD			0x0080
#define TASK_WAKEKILL			0x0100
#define TASK_WAKING			0x0200
#define TASK_NOLOAD			0x0400
#define TASK_NEW			0x0800
 
#define TASK_RTLOCK_WAIT		0x1000
#define TASK_STATE_MAX			0x2000

 
#define TASK_KILLABLE			(TASK_WAKEKILL | TASK_UNINTERRUPTIBLE)
#define TASK_STOPPED			(TASK_WAKEKILL | __TASK_STOPPED)
#define TASK_TRACED			__TASK_TRACED

#define TASK_IDLE			(TASK_UNINTERRUPTIBLE | TASK_NOLOAD)

 
#define TASK_NORMAL			(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)

 
#define TASK_REPORT			(TASK_RUNNING | TASK_INTERRUPTIBLE | \
					 TASK_UNINTERRUPTIBLE | __TASK_STOPPED | \
					 __TASK_TRACED | EXIT_DEAD | EXIT_ZOMBIE | \
					 TASK_PARKED)

#define task_is_running(task)		(READ_ONCE((task)->__state) == TASK_RUNNING)

#define task_is_traced(task)		((READ_ONCE(task->jobctl) & JOBCTL_TRACED) != 0)
#define task_is_stopped(task)		((READ_ONCE(task->jobctl) & JOBCTL_STOPPED) != 0)
#define task_is_stopped_or_traced(task)	((READ_ONCE(task->jobctl) & (JOBCTL_STOPPED | JOBCTL_TRACED)) != 0)

 
#define is_special_task_state(state)				\
	((state) & (__TASK_STOPPED | __TASK_TRACED | TASK_PARKED | TASK_DEAD))

# define debug_normal_state_change(cond)	do { } while (0)
# define debug_special_state_change(cond)	do { } while (0)
# define debug_rtlock_wait_set_state()		do { } while (0)
# define debug_rtlock_wait_restore_state()	do { } while (0)

 
#define __set_current_state(state_value)				\
	do {								\
		debug_normal_state_change((state_value));		\
		WRITE_ONCE(current->__state, (state_value));		\
	} while (0)

#define set_current_state(state_value)					\
	do {								\
		debug_normal_state_change((state_value));		\
		smp_store_mb(current->__state, (state_value));		\
	} while (0)

 
#define set_special_state(state_value)					\
	do {								\
		unsigned long flags;  			\
									\
		raw_spin_lock_irqsave(&current->pi_lock, flags);	\
		debug_special_state_change((state_value));		\
		WRITE_ONCE(current->__state, (state_value));		\
		raw_spin_unlock_irqrestore(&current->pi_lock, flags);	\
	} while (0)

 
#define current_save_and_set_rtlock_wait_state()			\
	do {								\
		lockdep_assert_irqs_disabled();				\
		raw_spin_lock(&current->pi_lock);			\
		current->saved_state = current->__state;		\
		debug_rtlock_wait_set_state();				\
		WRITE_ONCE(current->__state, TASK_RTLOCK_WAIT);		\
		raw_spin_unlock(&current->pi_lock);			\
	} while (0);

#define current_restore_rtlock_saved_state()				\
	do {								\
		lockdep_assert_irqs_disabled();				\
		raw_spin_lock(&current->pi_lock);			\
		debug_rtlock_wait_restore_state();			\
		WRITE_ONCE(current->__state, current->saved_state);	\
		current->saved_state = TASK_RUNNING;			\
		raw_spin_unlock(&current->pi_lock);			\
	} while (0);

#define get_current_state()	READ_ONCE(current->__state)

 
enum {
	TASK_COMM_LEN = 16,
};

extern void scheduler_tick(void);

#define	MAX_SCHEDULE_TIMEOUT		LONG_MAX

extern long schedule_timeout(long timeout);
extern long schedule_timeout_interruptible(long timeout);
extern long schedule_timeout_killable(long timeout);
extern long schedule_timeout_uninterruptible(long timeout);
extern long schedule_timeout_idle(long timeout);
asmlinkage void schedule(void);
extern void schedule_preempt_disabled(void);
asmlinkage void preempt_schedule_irq(void);

extern int __must_check io_schedule_prepare(void);
extern void io_schedule_finish(int token);
extern long io_schedule_timeout(long timeout);
extern void io_schedule(void);

 
struct prev_cputime {
	u64				utime;
	u64				stime;
	raw_spinlock_t			lock;
};

enum vtime_state {
	 
	VTIME_INACTIVE = 0,
	 
	VTIME_IDLE,
	 
	VTIME_SYS,
	 
	VTIME_USER,
	 
	VTIME_GUEST,
};

struct vtime {
	seqcount_t		seqcount;
	unsigned long long	starttime;
	enum vtime_state	state;
	unsigned int		cpu;
	u64			utime;
	u64			stime;
	u64			gtime;
};

 
enum uclamp_id {
	UCLAMP_MIN = 0,
	UCLAMP_MAX,
	UCLAMP_CNT
};


struct sched_info {
};

 
# define SCHED_FIXEDPOINT_SHIFT		10
# define SCHED_FIXEDPOINT_SCALE		(1L << SCHED_FIXEDPOINT_SHIFT)

 
# define SCHED_CAPACITY_SHIFT		SCHED_FIXEDPOINT_SHIFT
# define SCHED_CAPACITY_SCALE		(1L << SCHED_CAPACITY_SHIFT)

struct load_weight {
	unsigned long			weight;
	u32				inv_weight;
};

 
struct util_est {
	unsigned int			enqueued;
	unsigned int			ewma;
#define UTIL_EST_WEIGHT_SHIFT		2
#define UTIL_AVG_UNCHANGED		0x80000000
} __attribute__((__aligned__(sizeof(u64))));

 
struct sched_avg {
	u64				last_update_time;
	u64				load_sum;
	u64				runnable_sum;
	u32				util_sum;
	u32				period_contrib;
	unsigned long			load_avg;
	unsigned long			runnable_avg;
	unsigned long			util_avg;
	struct util_est			util_est;
} ____cacheline_aligned;

struct sched_statistics {
} ____cacheline_aligned;

struct sched_entity {
	 
	struct load_weight		load;
	struct rb_node			run_node;
	struct list_head		group_node;
	unsigned int			on_rq;

	u64				exec_start;
	u64				sum_exec_runtime;
	u64				vruntime;
	u64				prev_sum_exec_runtime;

	u64				nr_migrations;


};

struct sched_rt_entity {
	struct list_head		run_list;
	unsigned long			timeout;
	unsigned long			watchdog_stamp;
	unsigned int			time_slice;
	unsigned short			on_rq;
	unsigned short			on_list;

	struct sched_rt_entity		*back;
} __randomize_layout;

struct sched_dl_entity {
	struct rb_node			rb_node;

	 
	u64				dl_runtime;	 
	u64				dl_deadline;	 
	u64				dl_period;	 
	u64				dl_bw;		 
	u64				dl_density;	 

	 
	s64				runtime;	 
	u64				deadline;	 
	unsigned int			flags;		 

	 
	unsigned int			dl_throttled      : 1;
	unsigned int			dl_yielded        : 1;
	unsigned int			dl_non_contending : 1;
	unsigned int			dl_overrun	  : 1;

	 
	struct hrtimer			dl_timer;

	 
	struct hrtimer inactive_timer;

};


union rcu_special {
	struct {
		u8			blocked;
		u8			need_qs;
		u8			exp_hint;  
		u8			need_mb;  
	} b;  
	u32 s;  
};

enum perf_event_task_context {
	perf_invalid_context = -1,
	perf_hw_context = 0,
	perf_sw_context,
	perf_nr_task_contexts,
};

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




	struct sched_statistics         stats;



	unsigned int			policy;
	int				nr_cpus_allowed;
	const cpumask_t			*cpus_ptr;
	cpumask_t			*user_cpus_ptr;
	cpumask_t			cpus_mask;
	void				*migration_pending;
	unsigned short			migration_flags;




	struct sched_info		sched_info;

	struct list_head		tasks;

	struct mm_struct		*mm;
	struct mm_struct		*active_mm;

	 
	struct vmacache			vmacache;

#ifdef SPLIT_RSS_COUNTING
	struct task_rss_stat		rss_stat;
#endif
	int				exit_state;
	int				exit_code;
	int				exit_signal;
	 
	int				pdeath_signal;
	 
	unsigned long			jobctl;

	 
	unsigned int			personality;

	 
	unsigned			sched_reset_on_fork:1;
	unsigned			sched_contributes_to_load:1;
	unsigned			sched_migrated:1;

	 
	unsigned			:0;

	 

	 
	unsigned			sched_remote_wakeup:1;

	 
	unsigned			in_execve:1;
	unsigned			in_iowait:1;
#ifndef TIF_RESTORE_SIGMASK
	unsigned			restore_sigmask:1;
#endif
	unsigned			reported_split_lock:1;

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

	struct completion		*vfork_done;

	 
	int __user			*set_child_tid;

	 
	int __user			*clear_child_tid;

	 
	void				*worker_private;

	u64				utime;
	u64				stime;
	u64				gtime;
	struct prev_cputime		prev_cputime;

	 
	unsigned long			nvcsw;
	unsigned long			nivcsw;

	 
	u64				start_time;

	 
	u64				start_boottime;

	 
	unsigned long			min_flt;
	unsigned long			maj_flt;

	 
	struct posix_cputimers		posix_cputimers;


	 

	 
	const struct cred __rcu		*ptracer_cred;

	 
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
	sigset_t			real_blocked;
	 
	sigset_t			saved_sigmask;
	struct sigpending		pending;
	unsigned long			sas_ss_sp;
	size_t				sas_ss_size;
	unsigned int			sas_ss_flags;

	struct callback_head		*task_works;

	struct seccomp			seccomp;
	struct syscall_user_dispatch	syscall_dispatch;

	 
	u64				parent_exec_id;
	u64				self_exec_id;

	 
	spinlock_t			alloc_lock;

	 
	raw_spinlock_t			pi_lock;

	struct wake_q_node		wake_q;







	 
	void				*journal_info;

	 
	struct bio_list			*bio_list;

	 
	struct blk_plug			*plug;

	 
	struct reclaim_state		*reclaim_state;

	struct backing_dev_info		*backing_dev_info;

	struct io_context		*io_context;

	 
	unsigned long			ptrace_message;
	kernel_siginfo_t		*last_siginfo;

	struct task_io_accounting	ioac;
	struct perf_event_context	*perf_event_ctxp[perf_nr_task_contexts];
	struct mutex			perf_event_mutex;
	struct list_head		perf_event_list;


	struct tlbflush_unmap_batch	tlb_ubc;

	union {
		refcount_t		rcu_users;
		struct rcu_head		rcu;
	};

	 
	struct pipe_inode_info		*splice_pipe;

	struct page_frag		task_frag;


	 
	int				nr_dirtied;
	int				nr_dirtied_pause;
	 
	unsigned long			dirty_paused_when;

	 
	u64				timer_slack_ns;
	u64				default_timer_slack_ns;



#if IS_ENABLED(CONFIG_KUNIT)
	struct kunit			*kunit_test;
#endif






#if defined(CONFIG_BCACHE) || defined(CONFIG_BCACHE_MODULE)
	unsigned int			sequential_io;
	unsigned int			sequential_io_avg;
#endif
	struct kmap_ctrl		kmap_ctrl;
	int				pagefault_disabled;
	struct task_struct		*oom_reaper_list;
	struct timer_list		oom_reaper_timer;
	 
	refcount_t			stack_refcount;




	 
	struct callback_head		l1d_flush_kill;

	 
	randomized_struct_fields_end

	 
	struct thread_struct		thread;

	 
};

static inline struct pid *task_pid(struct task_struct *task)
{
	return task->thread_pid;
}

 
pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type, struct pid_namespace *ns);

static inline pid_t task_pid_nr(struct task_struct *tsk)
{
	return tsk->pid;
}

static inline pid_t task_pid_nr_ns(struct task_struct *tsk, struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PID, ns);
}

static inline pid_t task_pid_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PID, NULL);
}


static inline pid_t task_tgid_nr(struct task_struct *tsk)
{
	return tsk->tgid;
}


static inline pid_t task_tgid_nr_ns(struct task_struct *tsk, struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_TGID, ns);
}

static inline pid_t task_tgid_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_TGID, NULL);
}


static inline int is_global_init(struct task_struct *tsk)
{
	return task_tgid_nr(tsk) == 1;
}

extern struct pid *cad_pid;

 
#define PF_VCPU			0x00000001	 
#define PF_IDLE			0x00000002	 
#define PF_EXITING		0x00000004	 
#define PF_POSTCOREDUMP		0x00000008	 
#define PF_IO_WORKER		0x00000010	 
#define PF_WQ_WORKER		0x00000020	 
#define PF_FORKNOEXEC		0x00000040	 
#define PF_MCE_PROCESS		0x00000080       
#define PF_SUPERPRIV		0x00000100	 
#define PF_DUMPCORE		0x00000200	 
#define PF_SIGNALED		0x00000400	 
#define PF_MEMALLOC		0x00000800	 
#define PF_NPROC_EXCEEDED	0x00001000	 
#define PF_USED_MATH		0x00002000	 
#define PF_NOFREEZE		0x00008000	 
#define PF_FROZEN		0x00010000	 
#define PF_KSWAPD		0x00020000	 
#define PF_MEMALLOC_NOFS	0x00040000	 
#define PF_MEMALLOC_NOIO	0x00080000	 
#define PF_LOCAL_THROTTLE	0x00100000	 
#define PF_KTHREAD		0x00200000	 
#define PF_RANDOMIZE		0x00400000	 
#define PF_NO_SETAFFINITY	0x04000000	 
#define PF_MCE_EARLY		0x08000000       
#define PF_MEMALLOC_PIN		0x10000000	 
#define PF_FREEZER_SKIP		0x40000000	 
#define PF_SUSPEND_TASK		0x80000000       

 
#define clear_stopped_child_used_math(child)	do { (child)->flags &= ~PF_USED_MATH; } while (0)
#define set_stopped_child_used_math(child)	do { (child)->flags |= PF_USED_MATH; } while (0)
#define clear_used_math()			clear_stopped_child_used_math(current)
#define set_used_math()				set_stopped_child_used_math(current)

#define conditional_stopped_child_used_math(condition, child) \
	do { (child)->flags &= ~PF_USED_MATH, (child)->flags |= (condition) ? PF_USED_MATH : 0; } while (0)

#define conditional_used_math(condition)	conditional_stopped_child_used_math(condition, current)

#define copy_to_stopped_child_used_math(child) \
	do { (child)->flags &= ~PF_USED_MATH, (child)->flags |= current->flags & PF_USED_MATH; } while (0)

 
#define tsk_used_math(p)			((p)->flags & PF_USED_MATH)
#define used_math()				tsk_used_math(current)


#define PFA_NO_NEW_PRIVS		0
#define PFA_SPREAD_PAGE			1	 
#define PFA_SPREAD_SLAB			2	 
#define PFA_SPEC_SSB_DISABLE		3	 
#define PFA_SPEC_SSB_FORCE_DISABLE	4	 
#define PFA_SPEC_IB_DISABLE		5	 
#define PFA_SPEC_IB_FORCE_DISABLE	6	 
#define PFA_SPEC_SSB_NOEXEC		7	 

#define TASK_PFA_TEST(name, func)					\
	static inline bool task_##func(struct task_struct *p)		\
	{ return test_bit(PFA_##name, &p->atomic_flags); }

#define TASK_PFA_SET(name, func)					\
	static inline void task_set_##func(struct task_struct *p)	\
	{ set_bit(PFA_##name, &p->atomic_flags); }

#define TASK_PFA_CLEAR(name, func)					\
	static inline void task_clear_##func(struct task_struct *p)	\
	{ clear_bit(PFA_##name, &p->atomic_flags); }

TASK_PFA_TEST(NO_NEW_PRIVS, no_new_privs)
TASK_PFA_SET(NO_NEW_PRIVS, no_new_privs)

TASK_PFA_TEST(SPREAD_PAGE, spread_page)
TASK_PFA_SET(SPREAD_PAGE, spread_page)
TASK_PFA_CLEAR(SPREAD_PAGE, spread_page)

TASK_PFA_TEST(SPREAD_SLAB, spread_slab)
TASK_PFA_SET(SPREAD_SLAB, spread_slab)
TASK_PFA_CLEAR(SPREAD_SLAB, spread_slab)

TASK_PFA_TEST(SPEC_SSB_DISABLE, spec_ssb_disable)
TASK_PFA_SET(SPEC_SSB_DISABLE, spec_ssb_disable)
TASK_PFA_CLEAR(SPEC_SSB_DISABLE, spec_ssb_disable)

TASK_PFA_TEST(SPEC_SSB_NOEXEC, spec_ssb_noexec)
TASK_PFA_SET(SPEC_SSB_NOEXEC, spec_ssb_noexec)
TASK_PFA_CLEAR(SPEC_SSB_NOEXEC, spec_ssb_noexec)

TASK_PFA_TEST(SPEC_SSB_FORCE_DISABLE, spec_ssb_force_disable)
TASK_PFA_SET(SPEC_SSB_FORCE_DISABLE, spec_ssb_force_disable)

TASK_PFA_TEST(SPEC_IB_DISABLE, spec_ib_disable)
TASK_PFA_SET(SPEC_IB_DISABLE, spec_ib_disable)
TASK_PFA_CLEAR(SPEC_IB_DISABLE, spec_ib_disable)

TASK_PFA_TEST(SPEC_IB_FORCE_DISABLE, spec_ib_force_disable)
TASK_PFA_SET(SPEC_IB_FORCE_DISABLE, spec_ib_force_disable)

static inline void
current_restore_flags(unsigned long orig_flags, unsigned long flags)
{
	current->flags &= ~flags;
	current->flags |= orig_flags & flags;
}

extern int cpuset_cpumask_can_shrink(const struct cpumask *cur, const struct cpumask *trial);
extern int task_can_attach(struct task_struct *p, const struct cpumask *cs_cpus_allowed);
static inline void do_set_cpus_allowed(struct task_struct *p, const struct cpumask *new_mask)
{
}
static inline int set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask)
{
	if (!cpumask_test_cpu(0, new_mask))
		return -EINVAL;
	return 0;
}
static inline int dup_user_cpus_ptr(struct task_struct *dst, struct task_struct *src, int node)
{
	if (src->user_cpus_ptr)
		return -EINVAL;
	return 0;
}
static inline void release_user_cpus_ptr(struct task_struct *p)
{
	WARN_ON(p->user_cpus_ptr);
}

static inline int dl_task_check_affinity(struct task_struct *p, const struct cpumask *mask)
{
	return 0;
}

extern int yield_to(struct task_struct *p, bool preempt);
extern void set_user_nice(struct task_struct *p, long nice);
extern int task_prio(const struct task_struct *p);

 
static inline int task_nice(const struct task_struct *p)
{
	return PRIO_TO_NICE((p)->static_prio);
}

extern int can_nice(const struct task_struct *p, const int nice);
extern int task_curr(const struct task_struct *p);
extern int idle_cpu(int cpu);
extern int available_idle_cpu(int cpu);
extern int sched_setscheduler(struct task_struct *, int, const struct sched_param *);
extern int sched_setscheduler_nocheck(struct task_struct *, int, const struct sched_param *);
extern void sched_set_fifo(struct task_struct *p);
extern void sched_set_fifo_low(struct task_struct *p);
extern void sched_set_normal(struct task_struct *p, int nice);
extern int sched_setattr(struct task_struct *, const struct sched_attr *);
extern int sched_setattr_nocheck(struct task_struct *, const struct sched_attr *);
extern struct task_struct *idle_task(int cpu);

 
static __always_inline bool is_idle_task(const struct task_struct *p)
{
	return !!(p->flags & PF_IDLE);
}

extern struct task_struct *curr_task(int cpu);
extern void ia64_set_curr_task(int cpu, struct task_struct *p);

void yield(void);

union thread_union {
	struct task_struct task;
	unsigned long stack[THREAD_SIZE/sizeof(long)];
};


extern unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)];

# define task_thread_info(task)	(&(task)->thread_info)

 

extern struct task_struct *find_task_by_vpid(pid_t nr);
extern struct task_struct *find_task_by_pid_ns(pid_t nr, struct pid_namespace *ns);

 
extern struct task_struct *find_get_task_by_vpid(pid_t nr);

extern int wake_up_state(struct task_struct *tsk, unsigned int state);
extern int wake_up_process(struct task_struct *tsk);
extern void wake_up_new_task(struct task_struct *tsk);

static inline void kick_process(struct task_struct *tsk) { }

extern void __set_task_comm(struct task_struct *tsk, const char *from, bool exec);

static inline void set_task_comm(struct task_struct *tsk, const char *from)
{
	__set_task_comm(tsk, from, false);
}

extern char *__get_task_comm(char *to, size_t len, struct task_struct *tsk);
#define get_task_comm(buf, tsk) ({			\
	BUILD_BUG_ON(sizeof(buf) != TASK_COMM_LEN);	\
	__get_task_comm(buf, sizeof(buf), tsk);		\
})

static inline unsigned long wait_task_inactive(struct task_struct *p, unsigned int match_state)
{
	return 1;
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


static inline int _cond_resched(void)
{
	return __cond_resched();
}



#define cond_resched() ({			\
	__might_resched(__FILE__, __LINE__, 0);	\
	_cond_resched();			\
})

extern int __cond_resched_lock(spinlock_t *lock);
extern int __cond_resched_rwlock_read(rwlock_t *lock);
extern int __cond_resched_rwlock_write(rwlock_t *lock);

#define MIGHT_RESCHED_RCU_SHIFT		8
#define MIGHT_RESCHED_PREEMPT_MASK	((1U << MIGHT_RESCHED_RCU_SHIFT) - 1)

 
# define PREEMPT_LOCK_RESCHED_OFFSETS	PREEMPT_LOCK_OFFSET

#define cond_resched_lock(lock) ({						\
	__might_resched(__FILE__, __LINE__, PREEMPT_LOCK_RESCHED_OFFSETS);	\
	__cond_resched_lock(lock);						\
})

#define cond_resched_rwlock_read(lock) ({					\
	__might_resched(__FILE__, __LINE__, PREEMPT_LOCK_RESCHED_OFFSETS);	\
	__cond_resched_rwlock_read(lock);					\
})

#define cond_resched_rwlock_write(lock) ({					\
	__might_resched(__FILE__, __LINE__, PREEMPT_LOCK_RESCHED_OFFSETS);	\
	__cond_resched_rwlock_write(lock);					\
})


 
static inline int spin_needbreak(spinlock_t *lock)
{
	return 0;
}

 
static inline int rwlock_needbreak(rwlock_t *lock)
{
	return 0;
}

static __always_inline bool need_resched(void)
{
	return unlikely(tif_need_resched());
}

 

static inline unsigned int task_cpu(const struct task_struct *p)
{
	return 0;
}

extern bool sched_task_on_rq(struct task_struct *p);
extern unsigned long get_wchan(struct task_struct *p);

 
#ifndef vcpu_is_preempted
static inline bool vcpu_is_preempted(int cpu)
{
	return false;
}
#endif

extern long sched_setaffinity(pid_t pid, const struct cpumask *new_mask);
extern long sched_getaffinity(pid_t pid, struct cpumask *mask);

#ifndef TASK_SIZE_OF
#define TASK_SIZE_OF(tsk)	TASK_SIZE
#endif



static inline void rseq_handle_notify_resume(struct ksignal *ksig,
					     struct pt_regs *regs)
{
}
static inline void rseq_signal_deliver(struct ksignal *ksig,
				       struct pt_regs *regs)
{
}
static inline void rseq_preempt(struct task_struct *t)
{
}
static inline void rseq_migrate(struct task_struct *t)
{
}
static inline void rseq_fork(struct task_struct *t, unsigned long clone_flags)
{
}
static inline void rseq_execve(struct task_struct *t)
{
}



static inline void rseq_syscall(struct pt_regs *regs)
{
}


static inline void sched_core_free(struct task_struct *tsk) { }
static inline void sched_core_fork(struct task_struct *p) { }

extern void sched_set_stop_task(int cpu, struct task_struct *stop);

#endif
