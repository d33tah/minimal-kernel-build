#ifndef _LINUX_RATELIMIT_TYPES_H
#define _LINUX_RATELIMIT_TYPES_H

#include <linux/bits.h>
#include <linux/param.h>
#include <linux/spinlock_types_raw.h>

struct ratelimit_state {
	raw_spinlock_t lock;
};

extern int ___ratelimit(struct ratelimit_state *rs, const char *func);
#define __ratelimit(state) ___ratelimit(state, __func__)

#endif
