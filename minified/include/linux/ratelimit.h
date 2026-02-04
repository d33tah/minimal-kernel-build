#ifndef _LINUX_RATELIMIT_H
#define _LINUX_RATELIMIT_H

#include <linux/ratelimit_types.h>
#include <linux/sched.h>
/* linux/spinlock.h removed - no spinlock usage */

/* ratelimit_state_init, ratelimit_set_flags removed - never called */

#define WARN_RATELIMIT(condition, format, ...)			\
({								\
	int rtn = WARN(condition, format, ##__VA_ARGS__);	\
	rtn;							\
})


#endif  
