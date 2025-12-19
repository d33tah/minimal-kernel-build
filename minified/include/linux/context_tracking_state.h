#ifndef _LINUX_CONTEXT_TRACKING_STATE_H
#define _LINUX_CONTEXT_TRACKING_STATE_H

#include <linux/percpu.h>
#include <linux/jump_label.h>

struct context_tracking {
	 
	bool active;
	int recursion;
	enum ctx_state {
		CONTEXT_DISABLED = -1,
		CONTEXT_KERNEL = 0,
		CONTEXT_USER,
	} state;
};

static __always_inline bool context_tracking_enabled(void) { return false; }

#endif
