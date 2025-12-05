#include <linux/sched/clock.h>
#include <linux/sched/cputime.h>
#include <linux/sched/debug.h>
#include <linux/sched/isolation.h>
#include <linux/sched/loadavg.h>
/* sched/nohz.h inlined */
static inline void wake_up_nohz_cpu(int cpu) { }
#include <linux/sched/mm.h>

#include <linux/sched/task_stack.h>

#include <linux/cpufreq.h>

#include <linux/cpuset.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>


#include <linux/irq.h>

#include <uapi/linux/membarrier.h> /* membarrier.h redirect */
#include <linux/mempolicy.h>
#include <linux/nmi.h>
#include <linux/nospec.h>
#include <linux/proc_fs.h>
#include <linux/psi.h>


#include <linux/timex.h>
#include <linux/utsname.h>



#include <uapi/linux/prctl.h>
#include <uapi/linux/sched/types.h>

#include <asm/switch_to.h>

#include "sched.h"
#include "sched-pelt.h"
#include "stats.h"
#include "autogroup.h"

#include "clock.c"






#include "loadavg.c"
#include "completion.c"
#include "swait.c"
#include "wait_bit.c"
#include "wait.c"






