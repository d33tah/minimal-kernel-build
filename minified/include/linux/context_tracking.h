#ifndef _LINUX_CONTEXT_TRACKING_H
#define _LINUX_CONTEXT_TRACKING_H
#include <linux/types.h>
#include <linux/compiler.h>
/* Context tracking disabled - minimal stubs */
enum ctx_state { CONTEXT_DISABLED = -1, CONTEXT_KERNEL = 0, CONTEXT_USER };
static __always_inline bool context_tracking_enabled(void) { return false; }
static inline enum ctx_state ct_state(void) { return CONTEXT_DISABLED; }
/* user_enter_irqoff, user_exit_irqoff, exception_enter/exit, context_tracking_init removed - unused */
#endif
