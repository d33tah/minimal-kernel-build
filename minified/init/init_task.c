#include <linux/init_task.h>
#include <linux/fs.h>
#include <linux/utsname.h>

struct nsproxy init_nsproxy = {
	.count = ATOMIC_INIT(1),
	.uts_ns = &init_uts_ns,
	.mnt_ns = NULL,
	.pid_ns_for_children = &init_pid_ns,
};

static struct signal_struct init_signals = {
	.shared_pending	= {
		.list = LIST_HEAD_INIT(init_signals.shared_pending.list),
		.signal =  {{0}}
	},
	.rlim		= INIT_RLIMITS,
	.exec_update_lock = __RWSEM_INITIALIZER(init_signals.exec_update_lock),
	.pids = {
		[PIDTYPE_PID]	= &init_struct_pid,
		[PIDTYPE_TGID]	= &init_struct_pid,
		[PIDTYPE_PGID]	= &init_struct_pid,
		[PIDTYPE_SID]	= &init_struct_pid,
	},
};

static struct sighand_struct init_sighand = {
	.count		= REFCOUNT_INIT(1),
	.action		= { { { .sa_handler = SIG_DFL, } }, },
	.siglock	= __SPIN_LOCK_UNLOCKED(init_sighand.siglock),
};

struct task_struct init_task __aligned(L1_CACHE_BYTES) = {
	.thread_info = INIT_THREAD_INFO(init_task),
	.__state = 0,
	.stack = init_stack,
	.flags = PF_KTHREAD,
	.mm = NULL,
	.active_mm = &init_mm,
	.se = {},
	.tasks = LIST_HEAD_INIT(init_task.tasks),
	.group_leader = &init_task,
	RCU_POINTER_INITIALIZER(real_cred, &init_cred),
	RCU_POINTER_INITIALIZER(cred, &init_cred),
	.comm = INIT_TASK_COMM,
	.thread = INIT_THREAD,
	.fs = &init_fs,
	.files = &init_files,
	.signal = &init_signals,
	.sighand = &init_sighand,
	.nsproxy = &init_nsproxy,
	.pending = { .list = LIST_HEAD_INIT(init_task.pending.list),
		     .signal = { { 0 } } },
	.alloc_lock = __SPIN_LOCK_UNLOCKED(init_task.alloc_lock),
	.pi_lock = __RAW_SPIN_LOCK_UNLOCKED(init_task.pi_lock),
	.thread_pid = &init_struct_pid,
};
