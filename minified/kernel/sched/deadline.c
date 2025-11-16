/* Minimal deadline scheduler stub */

static unsigned int sysctl_sched_dl_period_max = 1 << 22;
static unsigned int sysctl_sched_dl_period_min = 100;

void __dl_update(struct dl_bw *dl_b, s64 bw) { }
void __dl_sub(struct dl_bw *dl_b, u64 tsk_bw, int cpus) { }
void __dl_add(struct dl_bw *dl_b, u64 tsk_bw, int cpus) { }
void add_rq_bw(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq) { }
void sub_rq_bw(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq) { }
void add_running_bw(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq) { }
void sub_running_bw(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq) { }

void init_dl_bandwidth(struct dl_bandwidth *dl_b, u64 period, u64 runtime)
{
	raw_spin_lock_init(&dl_b->dl_runtime_lock);
	dl_b->dl_runtime = runtime;
}

void init_dl_bw(struct dl_bw *dl_b)
{
	raw_spin_lock_init(&dl_b->lock);
	if (global_rt_runtime() == RUNTIME_INF)
		dl_b->bw = -1;
	else
		dl_b->bw = to_ratio(global_rt_period(), global_rt_runtime());
	dl_b->total_bw = 0;
}

void init_dl_rq(struct dl_rq *dl_rq)
{
	dl_rq->root = RB_ROOT_CACHED;
}

void enqueue_pushable_dl_task(struct rq *rq, struct task_struct *p) { }
void dequeue_pushable_dl_task(struct rq *rq, struct task_struct *p) { }
void inc_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq) { }
void dec_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq) { }
void init_dl_task_timer(struct sched_dl_entity *dl_se) { }
int dl_runtime_exceeded(struct sched_dl_entity *dl_se) { return 0; }
void init_dl_inactive_task_timer(struct sched_dl_entity *dl_se) { }
void inc_dl_tasks(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq) { }
void dec_dl_tasks(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq) { }

static void enqueue_task_dl(struct rq *rq, struct task_struct *p, int flags) { }
static void dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags) { }
static void yield_task_dl(struct rq *rq) { }
static void check_preempt_curr_dl(struct rq *rq, struct task_struct *p, int flags) { }
static struct task_struct *pick_next_task_dl(struct rq *rq) { return NULL; }
static void put_prev_task_dl(struct rq *rq, struct task_struct *p) { }
static void set_next_task_dl(struct rq *rq, struct task_struct *p, bool first) { }
static void task_tick_dl(struct rq *rq, struct task_struct *p, int queued) { }
static void task_fork_dl(struct task_struct *p) { }
static void prio_changed_dl(struct rq *rq, struct task_struct *p, int oldprio) { }
static void switched_from_dl(struct rq *rq, struct task_struct *p) { }
static void switched_to_dl(struct rq *rq, struct task_struct *p) { }
static void update_curr_dl(struct rq *rq) { }

DEFINE_SCHED_CLASS(dl) = {
	.enqueue_task		= enqueue_task_dl,
	.dequeue_task		= dequeue_task_dl,
	.yield_task		= yield_task_dl,
	.check_preempt_curr	= check_preempt_curr_dl,
	.pick_next_task		= pick_next_task_dl,
	.put_prev_task		= put_prev_task_dl,
	.set_next_task		= set_next_task_dl,
	.task_tick		= task_tick_dl,
	.task_fork              = task_fork_dl,
	.prio_changed           = prio_changed_dl,
	.switched_from		= switched_from_dl,
	.switched_to		= switched_to_dl,
	.update_curr		= update_curr_dl,
};

int sched_dl_global_validate(void) { return 0; }
void sched_dl_do_global(void) { }
int sched_dl_overflow(struct task_struct *p, int policy, const struct sched_attr *attr) { return 0; }
void __setparam_dl(struct task_struct *p, const struct sched_attr *attr) { }
void __getparam_dl(struct task_struct *p, struct sched_attr *attr) { }
bool __checkparam_dl(const struct sched_attr *attr) { return false; }
bool dl_param_changed(struct task_struct *p, const struct sched_attr *attr) { return false; }

void __dl_clear_params(struct task_struct *p)
{
	struct sched_dl_entity *dl_se = &p->dl;
	dl_se->dl_runtime = 0;
	dl_se->dl_deadline = 0;
	dl_se->dl_period = 0;
	dl_se->flags = 0;
	dl_se->dl_bw = 0;
	dl_se->dl_density = 0;
}
