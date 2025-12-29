/* update_cfs_rq_load_avg, update_rt_rq_load_avg, update_dl_rq_load_avg,
   thermal_load_avg, update_irq_load_avg, rq_clock_pelt,
   update_thermal_load_avg removed - unused */

static inline void
update_rq_clock_pelt(struct rq *rq, s64 delta) { }

static inline void
update_idle_rq_clock_pelt(struct rq *rq) { }



