/* Minimal lockdep.h - stubs for !CONFIG_LOCKDEP */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

#include <linux/lockdep_types.h>

/* lockdep_init_task, lock_acquire, lock_release, lockdep_init,
   lockdep_init_map*, lockdep_set_class* all removed - no callers */
/* lockdep_off, lockdep_on removed - no callers */

/* lock_is_held, lockdep_is_held are never defined but used in
   RCU_LOCKDEP_WARN conditions (which are no-ops but still need valid syntax) */
#define lock_is_held(l)			(1)
#define lockdep_is_held(l)		(1)
/* lockdep_is_held_type removed - unused */

/* lockdep_assert removed - unused */

/* lockdep_assert_held removed - no callers */
/* lockdep_assert_not_held, lockdep_assert_held_write, lockdep_assert_held_read removed - unused */

/* lockdep_pin_lock/unpin_lock removed - no callers */
/* lockdep_repin_lock removed - unused */

/* STATIC_LOCKDEP_MAP_INIT removed - unused */

/* lock_contended/lock_acquired removed - no callers */

#define LOCK_CONTENDED(_lock, try, lock) \
	lock(_lock)

#define LOCK_CONTENDED_RETURN(_lock, try, lock) \
	lock(_lock)

#define SINGLE_DEPTH_NESTING			1

/* lock_acquire_*, spin_acquire/release, seqcount_acquire/release,
   mutex_acquire/release, rwsem_acquire/release all removed - no callers */

#endif  
