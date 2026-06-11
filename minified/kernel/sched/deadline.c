/* Minimal deadline scheduler stub */

/* __dl_update, __dl_sub, __dl_add, add_rq_bw, sub_rq_bw, add_running_bw, sub_running_bw,
   init_dl_bandwidth, init_dl_bw, sysctl_sched_dl_period_max/min removed - unused */

void init_dl_rq(struct dl_rq *dl_rq)
{
}

void init_dl_task_timer(struct sched_dl_entity *dl_se) { }
void init_dl_inactive_task_timer(struct sched_dl_entity *dl_se) { }

/*
 * No task is ever DEADLINE on this build, so the dl runqueue is never
 * populated. Only pick_next_task is dispatched (via for_each_class, returning
 * NULL on the empty dl_rq); every other member is only invoked for a task
 * already in the dl class (which never happens) and so was dead. Dropped (the
 * struct members default to NULL and are never dereferenced). task_fork is
 * NULL-checked at its dispatch site, the others are only reached for an
 * in-class task.
 */
static struct task_struct *pick_next_task_dl(struct rq *rq) { return NULL; }

DEFINE_SCHED_CLASS(dl) = {
	.pick_next_task		= pick_next_task_dl,
};

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
