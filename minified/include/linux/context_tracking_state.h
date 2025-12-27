#ifndef _LINUX_CONTEXT_TRACKING_STATE_H
#define _LINUX_CONTEXT_TRACKING_STATE_H
enum ctx_state { CONTEXT_DISABLED = -1, CONTEXT_KERNEL = 0, CONTEXT_USER };
static __always_inline bool context_tracking_enabled(void) { return false; }
#endif
