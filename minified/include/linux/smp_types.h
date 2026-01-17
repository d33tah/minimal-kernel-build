#ifndef __LINUX_SMP_TYPES_H
#define __LINUX_SMP_TYPES_H

#include <linux/llist.h>

/* CSD and IRQ_WORK enums removed - never used */


struct __call_single_node {
	struct llist_node	llist;
	union {
		unsigned int	u_flags;
		atomic_t	a_flags;
	};
};

#endif  
