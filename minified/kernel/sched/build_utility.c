#include <linux/sched/clock.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/isolation.h>
/* loadavg.h removed - LOAD_FREQ moved to sched.h */
#include <linux/sched/mm.h>

#include <linux/sched/task_stack.h>

#include <linux/sched/types.h>

#include <asm/switch_to.h>

#include "sched.h"
/* end sched-pelt.h */
#include "stats.h"

/* loadavg.c removed - calc_global_load/calc_global_load_tick calls removed */
unsigned long calc_load_update;

/* completion.c removed - stubs inlined into include/linux/completion.h */
#include "swait.c"
#include "wait_bit.c"
#include "wait.c"
