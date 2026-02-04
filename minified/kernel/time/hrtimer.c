/* hrtimer.c now essentially empty - hrtimer_run_queues and hrtimers_init
   both removed as they were empty stubs. All other functions were previously
   removed. File kept only for includes needed by hrtimer.h types. */
/* linux/export.h removed - no EXPORT_SYMBOL */
#include <linux/percpu.h>
#include <linux/hrtimer.h>

#include "tick-internal.h"
