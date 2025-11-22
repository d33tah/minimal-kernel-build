/* Stub: Load average calculation - not needed for minimal kernel */

atomic_long_t calc_load_tasks;
unsigned long calc_load_update;
unsigned long avenrun[3];

/* Stub: get_avenrun - return zeros */
void get_avenrun(unsigned long *loads, unsigned long offset, int shift)
{
	loads[0] = 0;
	loads[1] = 0;
	loads[2] = 0;
}

/* Stub: calc_load_fold_active - no load tracking for minimal kernel */
long calc_load_fold_active(struct rq *this_rq, long adjust)
{
	return 0;
}

/* Stub: calc_load_n - not needed for minimal kernel */
unsigned long
calc_load_n(unsigned long load, unsigned long exp,
	    unsigned long active, unsigned int n)
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
