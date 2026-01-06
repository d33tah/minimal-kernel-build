#include <linux/sched/clock.h>
#include <linux/sched/cputime.h>
#include <linux/sched/debug.h>
#include <linux/sched/isolation.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/mm.h>

#include <linux/sched/task_stack.h>

/* linux/cpuset.h removed - empty header */
#include <linux/ctype.h>

#include <linux/irq.h>

#include <linux/mempolicy.h>
#include <linux/nospec.h>
#include <linux/proc_fs.h>

#include <linux/timex.h>
#include <linux/utsname.h>

#include <linux/sched/types.h>

#include <asm/switch_to.h>

#include "sched.h"
/* runnable_avg_yN_inv, LOAD_AVG_PERIOD, LOAD_AVG_MAX removed - unused */
/* end sched-pelt.h */
#include "stats.h"

#include "clock.c"

/* loadavg.c removed - calc_global_load/calc_global_load_tick calls removed */
/* Keep variable definitions needed by core.c */
atomic_long_t calc_load_tasks;
unsigned long calc_load_update;

#include "completion.c"
#include "swait.c"
#include "wait_bit.c"
#include "wait.c"
