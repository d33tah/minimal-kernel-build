
static inline int
update_cfs_rq_load_avg(u64 now, struct cfs_rq *cfs_rq)
{
	return 0;
}

static inline int
update_rt_rq_load_avg(u64 now, struct rq *rq, int running)
{
	return 0;
}

static inline int
update_dl_rq_load_avg(u64 now, struct rq *rq, int running)
{
	return 0;
}

static inline int
update_thermal_load_avg(u64 now, struct rq *rq, u64 capacity)
{
	return 0;
}

static inline u64 thermal_load_avg(struct rq *rq)
{
	return 0;
}

static inline int
update_irq_load_avg(struct rq *rq, u64 running)
{
	return 0;
}

static inline u64 rq_clock_pelt(struct rq *rq)
{
	return rq_clock_task(rq);
}

static inline void
update_rq_clock_pelt(struct rq *rq, s64 delta) { }

static inline void
update_idle_rq_clock_pelt(struct rq *rq) { }



