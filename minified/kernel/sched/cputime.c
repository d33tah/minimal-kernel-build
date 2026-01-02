/* Simplified CPU time accounting for minimal kernel
 * Hello World doesn't need accurate CPU time tracking
 */

#include <linux/sched.h>
#include <linux/kernel_stat.h>

#include "sched.h"

/* All account_* functions and thread_group_cputime_adjusted removed - never called */
