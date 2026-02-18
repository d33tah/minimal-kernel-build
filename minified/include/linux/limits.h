#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

#define ARG_MAX       131072
#define NAME_MAX         255
#define PATH_MAX        4096

#include <linux/types.h>

#define USHRT_MAX	((unsigned short)~0U)
#define INT_MAX		((int)(~0U >> 1))
#define INT_MIN		(-INT_MAX - 1)
#define UINT_MAX	(~0U)
#define LONG_MAX	((long)(~0UL >> 1))
#define ULONG_MAX	(~0UL)
#define ULLONG_MAX	(~0ULL)

#define SIZE_MAX	(~(size_t)0)
#define PHYS_ADDR_MAX	(~(phys_addr_t)0)


#endif  
