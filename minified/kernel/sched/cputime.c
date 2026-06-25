

static inline void task_group_account_field(struct task_struct *p, int index,
					    u64 tmp)
{
	 
	__this_cpu_add(kernel_cpustat.cpustat[index], tmp);
}

void account_process_tick(struct task_struct *p, int user_tick)
{
	u64 cputime = TICK_NSEC;

	if (user_tick) {
		int index;

		account_group_user_time(p, cputime);

		index = (task_nice(p) > 0) ? CPUTIME_NICE : CPUTIME_USER;

		task_group_account_field(p, index, cputime);
	} else if ((p != this_rq()->idle) || (irq_count() != HARDIRQ_OFFSET)) {
		int index;

		if (hardirq_count() - HARDIRQ_OFFSET)
			index = CPUTIME_IRQ;
		else if (in_serving_softirq())
			index = CPUTIME_SOFTIRQ;
		else
			index = CPUTIME_SYSTEM;

		account_group_system_time(p, cputime);

		task_group_account_field(p, index, cputime);
	} else {
		u64 *cpustat = kcpustat_this_cpu->cpustat;
		struct rq *rq = this_rq();

		if (atomic_read(&rq->nr_iowait) > 0)
			cpustat[CPUTIME_IOWAIT] += cputime;
		else
			cpustat[CPUTIME_IDLE] += cputime;
	}
}

