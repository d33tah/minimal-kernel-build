#ifndef _LINUX_NS_COMMON_H
#define _LINUX_NS_COMMON_H

#include <linux/refcount.h>

struct ns_common { unsigned int inum; refcount_t count; };

#endif
