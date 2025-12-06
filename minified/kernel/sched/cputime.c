

#define sched_clock_irqtime	(0)

static inline void task_group_account_field(struct task_struct *p, int index,
					    u64 tmp)
{
	 
	__this_cpu_add(kernel_cpustat.cpustat[index], tmp);

	cgroup_account_cputime_field(p, index, tmp);
}

void account_user_time(struct task_struct *p, u64 cputime)
{
	int index;

	 
	p->utime += cputime;
	account_group_user_time(p, cputime);

	index = (task_nice(p) > 0) ? CPUTIME_NICE : CPUTIME_USER;

	 
	task_group_account_field(p, index, cputime);

	 
	acct_account_cputime(p);
}

void account_guest_time(struct task_struct *p, u64 cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	 
	p->utime += cputime;
	account_group_user_time(p, cputime);
	p->gtime += cputime;

	 
	if (task_nice(p) > 0) {
		task_group_account_field(p, CPUTIME_NICE, cputime);
		cpustat[CPUTIME_GUEST_NICE] += cputime;
	} else {
		task_group_account_field(p, CPUTIME_USER, cputime);
		cpustat[CPUTIME_GUEST] += cputime;
	}
}

void account_system_index_time(struct task_struct *p,
			       u64 cputime, enum cpu_usage_stat index)
{
	 
	p->stime += cputime;
	account_group_system_time(p, cputime);

	 
	task_group_account_field(p, index, cputime);

	 
	acct_account_cputime(p);
}

void account_system_time(struct task_struct *p, int hardirq_offset, u64 cputime)
{
	int index;

	if ((p->flags & PF_VCPU) && (irq_count() - hardirq_offset == 0)) {
		account_guest_time(p, cputime);
		return;
	}

	if (hardirq_count() - hardirq_offset)
		index = CPUTIME_IRQ;
	else if (in_serving_softirq())
		index = CPUTIME_SOFTIRQ;
	else
		index = CPUTIME_SYSTEM;

	account_system_index_time(p, cputime, index);
}

/* Stub: account_steal_time not used in minimal kernel */
void account_steal_time(u64 cputime) { }

void account_idle_time(u64 cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	struct rq *rq = this_rq();

	if (atomic_read(&rq->nr_iowait) > 0)
		cpustat[CPUTIME_IOWAIT] += cputime;
	else
		cpustat[CPUTIME_IDLE] += cputime;
}

static __always_inline u64 steal_account_process_time(u64 maxtime)
{
	return 0;
}

/* Stubbed: thread_group_cputime not used externally */
void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times)
{
	times->utime = 0;
	times->stime = 0;
	times->sum_exec_runtime = 0;
}

static inline void irqtime_account_idle_ticks(int ticks) { }
static inline void irqtime_account_process_tick(struct task_struct *p, int user_tick,
						int nr_ticks) { }


void account_process_tick(struct task_struct *p, int user_tick)
{
	u64 cputime, steal;

	if (vtime_accounting_enabled_this_cpu())
		return;

	if (sched_clock_irqtime) {
		irqtime_account_process_tick(p, user_tick, 1);
		return;
	}

	cputime = TICK_NSEC;
	steal = steal_account_process_time(ULONG_MAX);

	if (steal >= cputime)
		return;

	cputime -= steal;

	if (user_tick)
		account_user_time(p, cputime);
	else if ((p != this_rq()->idle) || (irq_count() != HARDIRQ_OFFSET))
		account_system_time(p, HARDIRQ_OFFSET, cputime);
	else
		account_idle_time(cputime);
}

/* Stub: account_idle_ticks not used in minimal kernel */
void account_idle_ticks(unsigned long ticks)
{
}

/* Stubbed: cputime_adjust not used externally */
void cputime_adjust(struct task_cputime *curr, struct prev_cputime *prev,
		    u64 *ut, u64 *st)
{
	*ut = 0;
	*st = 0;
}

/* Stubbed: task_cputime_adjusted not used externally */
void task_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st)
{
	*ut = 0;
	*st = 0;
}

/* Stubbed: thread_group_cputime_adjusted not used externally */
void thread_group_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st)
{
	*ut = 0;
	*st = 0;
}

