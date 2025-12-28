#ifndef _LINUX_CONTEXT_TRACKING_H
#define _LINUX_CONTEXT_TRACKING_H
#include <linux/sched.h>
#include <linux/vtime.h>
enum ctx_state { CONTEXT_DISABLED = -1, CONTEXT_KERNEL = 0, CONTEXT_USER };
static __always_inline bool context_tracking_enabled(void) { return false; }
#include <asm/ptrace.h>
static inline void user_enter_irqoff(void) { }
static inline void user_exit_irqoff(void) { }
static inline enum ctx_state exception_enter(void) { return 0; }
static inline void exception_exit(enum ctx_state prev_ctx) { }
static inline enum ctx_state ct_state(void) { return CONTEXT_DISABLED; }
#define CT_WARN_ON(cond) WARN_ON(context_tracking_enabled() && (cond))
static inline void context_tracking_init(void) { }
#endif
