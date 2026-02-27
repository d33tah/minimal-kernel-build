/* Minimal lockdep.h - stubs for !CONFIG_LOCKDEP */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

/* lockdep_types: lock_class_key and lockdep_map defined in spinlock_types_raw.h */

#define lockdep_is_held(l)		(1)

#define SINGLE_DEPTH_NESTING			1

#endif  
