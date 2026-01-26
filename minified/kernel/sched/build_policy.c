
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>

#include <linux/sched/rt.h>

#include <linux/jiffies.h>
#include <linux/init_task.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <asm/errno.h>

#include <linux/sched/types.h>

#include "sched.h"

#include "stats.h"
#include "pelt.h"

#include "idle.c"

#include "rt.c"
/* deadline.c merged into rt.c */
