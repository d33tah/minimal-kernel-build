#ifndef __LINUX_DEBUG_LOCKING_H
#define __LINUX_DEBUG_LOCKING_H

#include <linux/atomic.h>
#include <linux/cache.h>

struct task_struct;

extern int debug_locks __read_mostly;
extern int debug_locks_silent __read_mostly;


static __always_inline int __debug_locks_off(void)
{
	return xchg(&debug_locks, 0);
}

extern int debug_locks_off(void);

#define DEBUG_LOCKS_WARN_ON(c)						\
({									\
	int __ret = 0;							\
									\
	if (!oops_in_progress && unlikely(c)) {				\
		if (debug_locks_off() && !debug_locks_silent)		\
			WARN(1, "DEBUG_LOCKS_WARN_ON(%s)", #c);		\
		__ret = 1;						\
	}								\
	__ret;								\
})

# define SMP_DEBUG_LOCKS_WARN_ON(c)			do { } while (0)

# define locking_selftest()	do { } while (0)

static inline void debug_show_all_locks(void)
{
}

static inline void debug_show_held_locks(struct task_struct *task)
{
}

static inline void
debug_check_no_locks_freed(const void *from, unsigned long len)
{
}

static inline void
debug_check_no_locks_held(void)
{
}

#endif
