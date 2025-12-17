/* Stub: Load average calculation - not needed for minimal kernel */

atomic_long_t calc_load_tasks;
unsigned long calc_load_update;
/* avenrun, get_avenrun removed - never called */

/* Stub: calc_load_fold_active - no load tracking for minimal kernel */
long calc_load_fold_active(struct rq *this_rq, long adjust)
{
	return 0;
}

/* Stub: calc_global_load - no load average calculation for minimal kernel */
void calc_global_load(void)
{
}

/* Stub: calc_global_load_tick - no load tracking for minimal kernel */
void calc_global_load_tick(struct rq *this_rq)
{
}
