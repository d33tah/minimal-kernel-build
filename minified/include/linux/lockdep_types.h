#ifndef __LINUX_LOCKDEP_TYPES_H
#define __LINUX_LOCKDEP_TYPES_H

#include <linux/types.h>

/* MAX_LOCKDEP_SUBCLASSES, enum lockdep_wait_type, enum lockdep_lock_type removed - unused */

struct lock_class_key { };

struct lockdep_map { };
/* pin_cookie removed - never referenced */

#endif  
