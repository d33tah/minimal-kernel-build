#include <linux/init_task.h>
#include <linux/sched/signal.h>

static struct signal_struct init_signals = {
	.rlim = INIT_RLIMITS,
	.cred_guard_mutex = __MUTEX_INITIALIZER(init_signals.cred_guard_mutex),
	.exec_update_lock = __RWSEM_INITIALIZER(init_signals.exec_update_lock),
};

struct task_struct init_task __aligned(L1_CACHE_BYTES) = {
	.thread_info = INIT_THREAD_INFO(init_task),
	.stack_refcount = REFCOUNT_INIT(1),
	.__state = 0,
	.stack = init_stack,
	.usage = REFCOUNT_INIT(2),
	.flags = PF_KTHREAD,
	.mm = NULL,
	.active_mm = &init_mm,
	.group_leader = &init_task,
	RCU_POINTER_INITIALIZER(real_cred, &init_cred),
	RCU_POINTER_INITIALIZER(cred, &init_cred),
	.comm = INIT_TASK_COMM,
	.thread = INIT_THREAD,
	.fs = &init_fs,
	.files = &init_files,
	.signal = &init_signals,
	.nsproxy = &init_nsproxy,
	.alloc_lock = __SPIN_LOCK_UNLOCKED(init_task.alloc_lock),
	.pi_lock = __RAW_SPIN_LOCK_UNLOCKED(init_task.pi_lock),
	.thread_pid = &init_struct_pid,
};
