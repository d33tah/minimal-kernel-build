/* Minimal lockdep.h - stubs for !CONFIG_LOCKDEP */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

/* lockdep_types: lock_class_key and lockdep_map defined in spinlock_types_raw.h */

/* lockdep_init_task, lock_acquire, lock_release, lockdep_init,
   lockdep_init_map*, lockdep_set_class* all removed - no callers */

/* lock_is_held, lockdep_is_held are never defined but used in
   RCU_LOCKDEP_WARN conditions (which are no-ops but still need valid syntax) */
#define lock_is_held(l)			(1)
#define lockdep_is_held(l)		(1)

#define SINGLE_DEPTH_NESTING			1

/* lock_acquire_*, spin_acquire/release, seqcount_acquire/release,
   mutex_acquire/release, rwsem_acquire/release all removed - no callers */

#endif  
