#include <linux/sched/clock.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/isolation.h>
#include <linux/sched/mm.h>

#include <linux/sched/task_stack.h>

#include <linux/sched/types.h>

#include <asm/switch_to.h>

#include "sched.h"

#include "swait.c"
#include "wait_bit.c"
#include "wait.c"
