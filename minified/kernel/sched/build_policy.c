
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>

#include <linux/sched/rt.h>

#include <linux/jiffies.h>
#include <linux/init_task.h>
#include <linux/slab.h>
/* linux/swap.h removed - no swap features used */
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <asm/errno.h>

#include <linux/sched/types.h>

#include "sched.h"
/* stats.h, pelt.h removed - were empty */

#include "idle.c"

#include "rt.c"
/* deadline.c merged into rt.c */
