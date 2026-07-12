
notrace unsigned long long __weak sched_clock(void)
{
	return (unsigned long long)(jiffies - INITIAL_JIFFIES)
					* (NSEC_PER_SEC / HZ);
}

static DEFINE_STATIC_KEY_FALSE(sched_clock_running);

static DEFINE_STATIC_KEY_FALSE(__sched_clock_stable);

__read_mostly u64 __sched_clock_offset;
static __read_mostly u64 __gtod_offset;

struct sched_clock_data {
	u64 tick_raw, tick_gtod, clock;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct sched_clock_data, sched_clock_data);

notrace static inline struct sched_clock_data *this_scd(void)
{
	return this_cpu_ptr(&sched_clock_data);
}

notrace static inline struct sched_clock_data *cpu_sdc(int cpu)
{
	return &per_cpu(sched_clock_data, cpu);
}

notrace int sched_clock_stable(void)
{
	return static_branch_likely(&__sched_clock_stable);
}

notrace static void __scd_stamp(struct sched_clock_data *scd)
{
	scd->tick_gtod = ktime_get_ns();
	scd->tick_raw = sched_clock();
}

notrace static void __set_sched_clock_stable(void)
{
	struct sched_clock_data *scd;

	 
	local_irq_disable();
	scd = this_scd();
	 
	__sched_clock_offset = (scd->tick_gtod + __gtod_offset) - (scd->tick_raw);
	local_irq_enable();

	printk(KERN_INFO "sched_clock: Marking stable (%lld, %lld)->(%lld, %lld)\n", scd->tick_gtod, __gtod_offset, scd->tick_raw,  __sched_clock_offset);

	static_branch_enable(&__sched_clock_stable);
}

notrace static void __sched_clock_gtod_offset(void)
{
	struct sched_clock_data *scd = this_scd();

	__scd_stamp(scd);
	__gtod_offset = (scd->tick_raw + __sched_clock_offset) - scd->tick_gtod;
}

void __init sched_clock_init(void)
{
	 
	local_irq_disable();
	__sched_clock_gtod_offset();
	local_irq_enable();

	static_branch_inc(&sched_clock_running);
}
static int __init sched_clock_init_late(void)
{
	static_branch_inc(&sched_clock_running);

	smp_mb();

	/* __sched_clock_stable_early was init=1 and its only clearer
	 * (clear_sched_clock_stable) had 0 callers -> constant true. */
	__set_sched_clock_stable();

	return 0;
}
late_initcall(sched_clock_init_late);


notrace static inline u64 wrap_min(u64 x, u64 y)
{
	return (s64)(x - y) < 0 ? x : y;
}

notrace static inline u64 wrap_max(u64 x, u64 y)
{
	return (s64)(x - y) > 0 ? x : y;
}

notrace static u64 sched_clock_local(struct sched_clock_data *scd)
{
	u64 now, clock, old_clock, min_clock, max_clock, gtod;
	s64 delta;

again:
	now = sched_clock();
	delta = now - scd->tick_raw;
	if (unlikely(delta < 0))
		delta = 0;

	old_clock = scd->clock;

	 

	gtod = scd->tick_gtod + __gtod_offset;
	clock = gtod + delta;
	min_clock = wrap_max(gtod, old_clock);
	max_clock = wrap_max(old_clock, gtod + TICK_NSEC);

	clock = wrap_max(clock, min_clock);
	clock = wrap_min(clock, max_clock);

	if (!try_cmpxchg64(&scd->clock, &old_clock, clock))
		goto again;

	return clock;
}

notrace u64 sched_clock_cpu(int cpu)
{
	struct sched_clock_data *scd;
	u64 clock;

	if (sched_clock_stable())
		return sched_clock() + __sched_clock_offset;

	if (!static_branch_likely(&sched_clock_running))
		return sched_clock();

	preempt_disable_notrace();
	scd = cpu_sdc(cpu);

	clock = sched_clock_local(scd);
	preempt_enable_notrace();

	return clock;
}

notrace void sched_clock_tick(void)
{
	struct sched_clock_data *scd;

	if (sched_clock_stable())
		return;

	if (!static_branch_likely(&sched_clock_running))
		return;

	lockdep_assert_irqs_disabled();

	scd = this_scd();
	__scd_stamp(scd);
	sched_clock_local(scd);
}

