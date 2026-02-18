#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#define CSIGNAL		0x000000ff
#define CLONE_VM	0x00000100
#define CLONE_FS	0x00000200
#define CLONE_FILES	0x00000400
#define CLONE_UNTRACED		0x00800000
#define SCHED_NORMAL		0
/* End uapi/linux/sched.h */

/* pid.h inlined */
#include <linux/rculist.h>
#include <linux/wait.h>
#include <linux/refcount.h>
enum pid_type {
	PIDTYPE_PID,
	PIDTYPE_TGID,
	PIDTYPE_PGID,
	PIDTYPE_SID,
	PIDTYPE_MAX,
};
struct upid {
	int nr;
	struct pid_namespace *ns;
};
struct pid {
	refcount_t count;
	unsigned int level;
	struct hlist_head tasks[PIDTYPE_MAX];
	struct rcu_head rcu;
	struct upid numbers[1];
};
extern struct pid init_struct_pid;
static inline struct pid *get_pid(struct pid *pid)
{
	if (pid)
		refcount_inc(&pid->count);
	return pid;
}
extern void put_pid(struct pid *pid);
extern struct pid *get_task_pid(struct task_struct *task, enum pid_type type);
extern void attach_pid(struct task_struct *task, enum pid_type);
struct pid_namespace;
extern struct pid_namespace init_pid_ns;
extern struct pid *alloc_pid(struct pid_namespace *ns, pid_t *set_tid,
			     size_t set_tid_size);
extern void free_pid(struct pid *pid);
static inline struct pid_namespace *ns_of_pid(struct pid *pid)
{
	struct pid_namespace *ns = NULL;
	if (pid)
		ns = pid->numbers[pid->level].ns;
	return ns;
}
static inline bool is_child_reaper(struct pid *pid)
{
	return pid->numbers[pid->level].nr == 1;
}
static inline pid_t pid_nr(struct pid *pid)
{
	pid_t nr = 0;
	if (pid)
		nr = pid->numbers[0].nr;
	return nr;
}
pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns);
pid_t pid_vnr(struct pid *pid);
#include <linux/rbtree.h>
#include <linux/rcupdate.h>
struct rlimit {
	__kernel_ulong_t	rlim_cur;
	__kernel_ulong_t	rlim_max;
};
#define _STK_LIM	(8*1024*1024)
#define RLIMIT_STACK		3
#define RLIMIT_NOFILE		7
#define RLIM_NLIMITS		16
#define RLIM_INFINITY		(~0UL)
#define INIT_RLIMITS {							\
	[0] = { RLIM_INFINITY, RLIM_INFINITY },				\
	[1] = { RLIM_INFINITY, RLIM_INFINITY },				\
	[2] = { RLIM_INFINITY, RLIM_INFINITY },				\
	[RLIMIT_STACK] = { 8*1024*1024, RLIM_INFINITY },		\
	[4] = { 0, RLIM_INFINITY },					\
	[5] = { RLIM_INFINITY, RLIM_INFINITY },				\
	[6] = { 0, 0 },						\
	[RLIMIT_NOFILE] = { INR_OPEN_CUR, INR_OPEN_MAX },		\
	[8] = { 8*1024*1024, 8*1024*1024 },				\
	[9] = { RLIM_INFINITY, RLIM_INFINITY },				\
	[10] = { RLIM_INFINITY, RLIM_INFINITY },			\
	[11] = { 0, 0 },						\
	[12] = { 819200, 819200 },					\
	[13] = { 0, 0 },						\
	[14] = { 0, 0 },						\
	[15] = { RLIM_INFINITY, RLIM_INFINITY },			\
}
/* end resource.h */
#define MAX_NICE	19
#define MIN_NICE	-20
#define NICE_WIDTH	(MAX_NICE - MIN_NICE + 1)
#define MAX_RT_PRIO		100
#define MAX_PRIO		(MAX_RT_PRIO + NICE_WIDTH)
/* end sched/prio.h */
struct sched_param { int sched_priority; };
#ifndef _LINUX_SIGNAL_TYPES_INLINED
#define _LINUX_SIGNAL_TYPES_INLINED
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
#ifndef _LINUX_MM_TYPES_TASK_H
#define _LINUX_MM_TYPES_TASK_H

#include <linux/threads.h>

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

#endif /* _LINUX_MM_TYPES_TASK_H */
#define KM_MAX_IDX 16

struct fs_struct;
struct nameidata;
struct nsproxy;
struct pid_namespace;
struct sighand_struct;
struct signal_struct;

#define TASK_RUNNING			0x0000
#define TASK_INTERRUPTIBLE		0x0001
#define TASK_UNINTERRUPTIBLE		0x0002
#define EXIT_DEAD			0x0010
#define TASK_PARKED			0x0040
#define TASK_DEAD			0x0080
#define TASK_WAKEKILL			0x0100
#define TASK_NEW			0x0800

#define TASK_KILLABLE			(TASK_WAKEKILL | TASK_UNINTERRUPTIBLE)

#define TASK_NORMAL			(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)

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
asmlinkage void schedule(void);
extern void schedule_preempt_disabled(void);
extern void io_schedule(void);

struct load_weight {
	unsigned long			weight;
};

struct sched_entity {
	struct load_weight		load;
	struct rb_node			run_node;
	unsigned int			on_rq;

	u64				exec_start;
	u64				sum_exec_runtime;
	u64				vruntime;
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

	int				on_rq;

	int				prio;
	int				static_prio;
	int				normal_prio;

	struct sched_entity		se;
	const struct sched_class	*sched_class;

	unsigned int			policy;
	const cpumask_t			*cpus_ptr;
	cpumask_t			cpus_mask;

	struct list_head		tasks;

	struct mm_struct		*mm;
	struct mm_struct		*active_mm;

	struct vmacache			vmacache;

	int				exit_state;
	int				exit_signal;
	 
	unsigned long			jobctl;

	unsigned int			personality;

	unsigned			restore_sigmask:1;

	unsigned long			atomic_flags;

	pid_t				pid;
	pid_t				tgid;

	struct task_struct __rcu	*real_parent;
	struct task_struct		*group_leader;

	struct pid			*thread_pid;
	struct hlist_node		pid_links[PIDTYPE_MAX];
	struct list_head		thread_group;
	struct list_head		thread_node;

	int __user			*set_child_tid;

	void				*worker_private;

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
	sigset_t			saved_sigmask;
	struct sigpending		pending;

	struct callback_head		*task_works;

	spinlock_t			alloc_lock;

	raw_spinlock_t			pi_lock;

	union {
		refcount_t		rcu_users;
		struct rcu_head		rcu;
	};

	struct kmap_ctrl		kmap_ctrl;
	int				pagefault_disabled;

	refcount_t			stack_refcount;

	randomized_struct_fields_end

	struct thread_struct		thread;

};

#define PF_IDLE			0x00000002
#define PF_EXITING		0x00000004
#define PF_WQ_WORKER		0x00000020
#define PF_FORKNOEXEC		0x00000040
#define PF_SUPERPRIV		0x00000100
#define PF_MEMALLOC		0x00000800
#define PF_NOFREEZE		0x00008000
#define PF_MEMALLOC_NOFS	0x00040000
#define PF_MEMALLOC_NOIO	0x00080000
#define PF_KTHREAD		0x00200000
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

static inline int set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask)
{
	if (!cpumask_test_cpu(0, new_mask))
		return -EINVAL;
	return 0;
}

extern int sched_setscheduler_nocheck(struct task_struct *, int, const struct sched_param *);

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

extern void __set_task_comm(struct task_struct *tsk, const char *from, bool exec);

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

static inline int test_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_ti_thread_flag(task_thread_info(tsk), flag);
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

#endif
