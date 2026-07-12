
static void cpuidle_idle_call(void) {

	if (need_resched()) {
		local_irq_enable();
		return; }

	/* folded sole caller of default_idle_call() */
	if (current_clr_polling_and_test()) {
		local_irq_enable();
	} else {


		stop_critical_timings();

		arch_cpu_idle();


		raw_local_irq_disable();
		raw_local_irq_enable();

		start_critical_timings();

	}

	__current_set_polling();


	if (WARN_ON_ONCE(irqs_disabled()))
		local_irq_enable(); }

static void do_idle(void) {
	__current_set_polling();

	while (!need_resched()) {
		rmb();

		local_irq_disable();

		cpuidle_idle_call(); }

	 
	preempt_set_need_resched();
	__current_clr_polling();

	 
	smp_mb__after_atomic();


	schedule_idle(); }

void cpu_startup_entry(enum cpuhp_state state) {
	while (1)
		do_idle(); }



static void check_preempt_curr_idle(struct rq *rq, struct task_struct *p, int flags) { }

static void put_prev_task_idle(struct rq *rq, struct task_struct *prev) { }

static void set_next_task_idle(struct rq *rq, struct task_struct *next, bool first) { }


struct task_struct *pick_next_task_idle(struct rq *rq) {
	struct task_struct *next = rq->idle;

	set_next_task_idle(rq, next, true);

	return next; }

static void
dequeue_task_idle(struct rq *rq, struct task_struct *p, int flags) { }

static void task_tick_idle(struct rq *rq, struct task_struct *curr, int queued) { }

DEFINE_SCHED_CLASS(idle) = { .dequeue_task		= dequeue_task_idle, .check_preempt_curr	= check_preempt_curr_idle, .pick_next_task		= pick_next_task_idle, .put_prev_task		= put_prev_task_idle, .set_next_task          = set_next_task_idle, .task_tick		= task_tick_idle, };
