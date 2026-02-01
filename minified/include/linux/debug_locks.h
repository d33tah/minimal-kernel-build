#ifndef __LINUX_DEBUG_LOCKING_H
#define __LINUX_DEBUG_LOCKING_H
#include <linux/atomic.h>
#include <linux/cache.h>
/* struct task_struct forward decl removed - unused */
extern int debug_locks __read_mostly;
/* debug_locks_silent removed - never set to non-zero */
static __always_inline int __debug_locks_off(void) { return xchg(&debug_locks, 0); }
extern int debug_locks_off(void);
#define DEBUG_LOCKS_WARN_ON(c) ({ int __ret = 0; if (!oops_in_progress && unlikely(c)) { if (debug_locks_off()) WARN(1, "DEBUG_LOCKS_WARN_ON(%s)", #c); __ret = 1; } __ret; })
#endif
