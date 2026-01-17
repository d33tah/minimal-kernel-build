#ifndef _LINUX_NS_COMMON_H
#define _LINUX_NS_COMMON_H
#include <linux/refcount.h>
/* struct proc_ns_operations forward decl removed - ops field removed */
struct ns_common { atomic_long_t stashed; unsigned int inum; refcount_t count; };
#endif
