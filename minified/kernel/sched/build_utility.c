#include <linux/sched/clock.h>
#include <linux/sched/cputime.h>
#include <linux/sched/debug.h>
#include <linux/sched/isolation.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/mm.h>

#include <linux/sched/task_stack.h>

#include <linux/cpuset.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>


#include <linux/irq.h>

#include <linux/mempolicy.h>
#include <linux/nmi.h>
#include <linux/nospec.h>
#include <linux/proc_fs.h>

#include <linux/timex.h>
#include <linux/utsname.h>



#include <linux/sched/types.h>

#include <asm/switch_to.h>

#include "sched.h"
/* sched-pelt.h inlined */
static const u32 runnable_avg_yN_inv[] __maybe_unused = {
	0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a, 0xeac0c6e6, 0xe5b906e6,
	0xe0ccdeeb, 0xdbfbb796, 0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85,
	0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46, 0xb504f333, 0xb123f581,
	0xad583ee9, 0xa9a15ab4, 0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9,
	0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a, 0x8b95c1e3, 0x88980e80,
	0x85aac367, 0x82cd8698,
};
#define LOAD_AVG_PERIOD 32
#define LOAD_AVG_MAX 47742
/* end sched-pelt.h */
#include "stats.h"
#include "autogroup.h"

#include "clock.c"






#include "loadavg.c"
#include "completion.c"
#include "swait.c"
#include "wait_bit.c"
#include "wait.c"






