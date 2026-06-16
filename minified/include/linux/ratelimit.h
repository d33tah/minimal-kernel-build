#ifndef _LINUX_RATELIMIT_H
#define _LINUX_RATELIMIT_H

#include <linux/ratelimit_types.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#define WARN_ON_RATELIMIT(condition, state)			\
	WARN_ON(condition)

#define WARN_RATELIMIT(condition, format, ...)			\
({								\
	int rtn = WARN(condition, format, ##__VA_ARGS__);	\
	rtn;							\
})


#endif  
