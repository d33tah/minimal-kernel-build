#include <linux/init_task.h>
#include <linux/export.h>
#include <linux/sched.h>
/* linux/sched/sysctl.h removed - sysctl_hung_task_timeout_secs unused */
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>

static struct signal_struct init_signals = {
	.nr_threads	= 1,
	.thread_head	= LIST_HEAD_INIT(init_task.thread_node),
	/* wait_chldexit removed */
	.shared_pending	= {
		.list = LIST_HEAD_INIT(init_signals.shared_pending.list),
		.signal =  {{0}}
	},
	.multiprocess	= HLIST_HEAD_INIT,
	.rlim		= INIT_RLIMITS,
	.cred_guard_mutex = __MUTEX_INITIALIZER(init_signals.cred_guard_mutex),
	.exec_update_lock = __RWSEM_INITIALIZER(init_signals.exec_update_lock),
	.pids = {
		[PIDTYPE_PID]	= &init_struct_pid,
		[PIDTYPE_TGID]	= &init_struct_pid,
		[PIDTYPE_PGID]	= &init_struct_pid,
		[PIDTYPE_SID]	= &init_struct_pid,
	},
	/* INIT_PREV_CPUTIME removed - prev_cputime removed from signal_struct */
};

static struct sighand_struct init_sighand = {
	.count		= REFCOUNT_INIT(1),
	.action		= { { { .sa_handler = SIG_DFL, } }, },
	.siglock	= __SPIN_LOCK_UNLOCKED(init_sighand.siglock),
	/* signalfd_wqh removed */
};

struct task_struct init_task
	__aligned(L1_CACHE_BYTES)
= {
	.thread_info	= INIT_THREAD_INFO(init_task),
	.stack_refcount	= REFCOUNT_INIT(1),
	.__state	= 0,
	.stack		= init_stack,
	.usage		= REFCOUNT_INIT(2),
	.flags		= PF_KTHREAD,
	.prio		= MAX_PRIO - 20,
	.static_prio	= MAX_PRIO - 20,
	.normal_prio	= MAX_PRIO - 20,
	.policy		= SCHED_NORMAL,
	.cpus_ptr	= &init_task.cpus_mask,
	.cpus_mask	= CPU_MASK_ALL,
	/* nr_cpus_allowed removed - never read */
	.mm		= NULL,
	.active_mm	= &init_mm,
	.restart_block	= {
		.fn = do_no_restart_syscall,
	},
	.se		= {
		/* .group_node removed - field removed from sched_entity */
	},
	.rt		= {
		/* .run_list, .time_slice removed - fields removed from sched_rt_entity */
	},
	.tasks		= LIST_HEAD_INIT(init_task.tasks),
	.ptraced	= LIST_HEAD_INIT(init_task.ptraced),
	.ptrace_entry	= LIST_HEAD_INIT(init_task.ptrace_entry),
	.real_parent	= &init_task,
	.parent		= &init_task,
	.children	= LIST_HEAD_INIT(init_task.children),
	.sibling	= LIST_HEAD_INIT(init_task.sibling),
	.group_leader	= &init_task,
	RCU_POINTER_INITIALIZER(real_cred, &init_cred),
	RCU_POINTER_INITIALIZER(cred, &init_cred),
	.comm		= INIT_TASK_COMM,
	.thread		= INIT_THREAD,
	.fs		= &init_fs,
	.files		= &init_files,
	.signal		= &init_signals,
	.sighand	= &init_sighand,
	.nsproxy	= &init_nsproxy,
	.pending	= {
		.list = LIST_HEAD_INIT(init_task.pending.list),
		.signal = {{0}}
	},
	.blocked	= {{0}},
	.alloc_lock	= __SPIN_LOCK_UNLOCKED(init_task.alloc_lock),
	.pi_lock	= __RAW_SPIN_LOCK_UNLOCKED(init_task.pi_lock),
	/* .timer_slack_ns removed - field removed from task_struct */
	.thread_pid	= &init_struct_pid,
	.thread_group	= LIST_HEAD_INIT(init_task.thread_group),
	.thread_node	= LIST_HEAD_INIT(init_signals.thread_head),
	/* perf_event_*, prev_cputime initializers removed - fields no longer exist */
};
