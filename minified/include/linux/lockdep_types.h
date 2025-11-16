 
 
#ifndef __LINUX_LOCKDEP_TYPES_H
#define __LINUX_LOCKDEP_TYPES_H

#include <linux/types.h>

#define MAX_LOCKDEP_SUBCLASSES		8UL

enum lockdep_wait_type {
	LD_WAIT_INV = 0,	 

	LD_WAIT_FREE,		 
	LD_WAIT_SPIN,		 

	LD_WAIT_CONFIG = LD_WAIT_SPIN,
	LD_WAIT_SLEEP,		 

	LD_WAIT_MAX,		 
};

enum lockdep_lock_type {
	LD_LOCK_NORMAL = 0,	 
	LD_LOCK_PERCPU,		 
	LD_LOCK_MAX,
};


 
struct lock_class_key { };

 
struct lockdep_map { };

struct pin_cookie { };


#endif  
