#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

#define __must_hold(x)
#define __acquires(x)
#define __releases(x)
#define __acquire(x)	(void)0
#define __release(x)	(void)0
#define __cond_lock(x,c) (c)

#include <linux/compiler-gcc.h>

#endif
