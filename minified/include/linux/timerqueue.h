#ifndef _LINUX_TIMERQUEUE_H
#define _LINUX_TIMERQUEUE_H

#include <linux/rbtree.h>
#include <linux/ktime.h>


struct timerqueue_node {
	struct rb_node node;
	ktime_t expires;
};

struct timerqueue_head {
	struct rb_root_cached rb_root;
};


/* timerqueue_add, timerqueue_del removed - never called */
/* timerqueue_iterate_next, timerqueue_getnext, timerqueue_init, timerqueue_init_head removed - never called */

#endif  
