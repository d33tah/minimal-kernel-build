 
#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/alarmtimer.h>
#include <linux/timerqueue.h>

struct kernel_siginfo;
struct task_struct;

 
#define CPUCLOCK_PID(clock)		((pid_t) ~((clock) >> 3))
#define CPUCLOCK_PERTHREAD(clock) \
	(((clock) & (clockid_t) CPUCLOCK_PERTHREAD_MASK) != 0)

#define CPUCLOCK_PERTHREAD_MASK	4
#define CPUCLOCK_WHICH(clock)	((clock) & (clockid_t) CPUCLOCK_CLOCK_MASK)
#define CPUCLOCK_CLOCK_MASK	3
#define CPUCLOCK_PROF		0
#define CPUCLOCK_VIRT		1
#define CPUCLOCK_SCHED		2
#define CPUCLOCK_MAX		3
#define CLOCKFD			CPUCLOCK_MAX
#define CLOCKFD_MASK		(CPUCLOCK_PERTHREAD_MASK|CPUCLOCK_CLOCK_MASK)

static inline clockid_t make_process_cpuclock(const unsigned int pid,
		const clockid_t clock)
{
	return ((~pid) << 3) | clock;
}
static inline clockid_t make_thread_cpuclock(const unsigned int tid,
		const clockid_t clock)
{
	return make_process_cpuclock(tid, clock | CPUCLOCK_PERTHREAD_MASK);
}

static inline clockid_t fd_to_clockid(const int fd)
{
	return make_process_cpuclock((unsigned int) fd, CLOCKFD);
}

static inline int clockid_to_fd(const clockid_t clk)
{
	return ~(clk >> 3);
}

struct posix_cputimers { };
struct cpu_timer { };
#define INIT_CPU_TIMERS(s)
static inline void posix_cputimers_init(struct posix_cputimers *pct) { }
static inline void posix_cputimers_group_init(struct posix_cputimers *pct,
					      u64 cpu_limit) { }

static inline void clear_posix_cputimers_work(struct task_struct *p) { }
static inline void posix_cputimers_init_work(void) { }

#define REQUEUE_PENDING 1

 
struct k_itimer {
	struct list_head	list;
	struct hlist_node	t_hash;
	spinlock_t		it_lock;
	const struct k_clock	*kclock;
	clockid_t		it_clock;
	timer_t			it_id;
	int			it_active;
	s64			it_overrun;
	s64			it_overrun_last;
	int			it_requeue_pending;
	int			it_sigev_notify;
	ktime_t			it_interval;
	struct signal_struct	*it_signal;
	union {
		struct pid		*it_pid;
		struct task_struct	*it_process;
	};
	struct sigqueue		*sigq;
	union {
		struct {
			struct hrtimer	timer;
		} real;
		struct cpu_timer	cpu;
		struct {
			struct alarm	alarmtimer;
		} alarm;
	} it;
	struct rcu_head		rcu;
};

void run_posix_cpu_timers(void);
void posix_cpu_timers_exit(struct task_struct *task);
void posix_cpu_timers_exit_group(struct task_struct *task);
void set_process_cpu_timer(struct task_struct *task, unsigned int clock_idx,
			   u64 *newval, u64 *oldval);

int update_rlimit_cpu(struct task_struct *task, unsigned long rlim_new);

void posixtimer_rearm(struct kernel_siginfo *info);
#endif
