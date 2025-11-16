 
#ifndef _LINUX_CONTEXT_TRACKING_STATE_H
#define _LINUX_CONTEXT_TRACKING_STATE_H

#include <linux/percpu.h>
#include <linux/static_key.h>

struct context_tracking {
	 
	bool active;
	int recursion;
	enum ctx_state {
		CONTEXT_DISABLED = -1,	 
		CONTEXT_KERNEL = 0,
		CONTEXT_USER,
		CONTEXT_GUEST,
	} state;
};

static __always_inline bool context_tracking_in_user(void) { return false; }
static __always_inline bool context_tracking_enabled(void) { return false; }
static __always_inline bool context_tracking_enabled_cpu(int cpu) { return false; }
static __always_inline bool context_tracking_enabled_this_cpu(void) { return false; }

#endif
