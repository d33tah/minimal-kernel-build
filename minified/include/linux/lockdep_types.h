#ifndef __LINUX_LOCKDEP_TYPES_H
#define __LINUX_LOCKDEP_TYPES_H

#include <linux/types.h>

/* Removed (0 users tree-wide): enum lockdep_wait_type + enum lockdep_lock_type
 * (all values 0-ref outside their own defs; lockdep is compiled out). */

struct lock_class_key { };

struct lockdep_map { };

struct pin_cookie { };


#endif  
