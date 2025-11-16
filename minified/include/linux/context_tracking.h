 
#ifndef _LINUX_CONTEXT_TRACKING_H
#define _LINUX_CONTEXT_TRACKING_H

#include <linux/sched.h>
#include <linux/vtime.h>
#include <linux/context_tracking_state.h>

#include <asm/ptrace.h>


static inline void user_enter(void) { }
static inline void user_exit(void) { }
static inline void user_enter_irqoff(void) { }
static inline void user_exit_irqoff(void) { }
static inline enum ctx_state exception_enter(void) { return 0; }
static inline void exception_exit(enum ctx_state prev_ctx) { }
static inline enum ctx_state ct_state(void) { return CONTEXT_DISABLED; }
static __always_inline bool context_tracking_guest_enter(void) { return false; }
static inline void context_tracking_guest_exit(void) { }


#define CT_WARN_ON(cond) WARN_ON(context_tracking_enabled() && (cond))

static inline void context_tracking_init(void) { }

#endif
