/* Minimal lockdep.h - stubs for !CONFIG_LOCKDEP */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

/* lockdep_types: lock_class_key and lockdep_map defined in spinlock_types_raw.h */

/* lock_is_held, lockdep_is_held are never defined but used in
   RCU_LOCKDEP_WARN conditions (which are no-ops but still need valid syntax) */
#define lock_is_held(l)			(1)
#define lockdep_is_held(l)		(1)

#define SINGLE_DEPTH_NESTING			1

#endif  
