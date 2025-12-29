#ifndef _LINUX_SCHED_LOADAVG_H
#define _LINUX_SCHED_LOADAVG_H
#define FSHIFT 11
#define FIXED_1 (1<<FSHIFT)
#define LOAD_FREQ (5*HZ+1)
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)
/* calc_global_load removed - declared but never called */
#endif  
