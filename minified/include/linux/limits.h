#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

/* Inlined from uapi/linux/limits.h */
#define ARG_MAX       131072
#define NAME_MAX         255
#define PATH_MAX        4096

#include <linux/types.h>

/* Inlined from vdso/limits.h */
#define USHRT_MAX	((unsigned short)~0U)
/* SHRT_MAX removed - unused */
#define INT_MAX		((int)(~0U >> 1))
#define INT_MIN		(-INT_MAX - 1)
#define UINT_MAX	(~0U)
#define LONG_MAX	((long)(~0UL >> 1))
/* LONG_MIN removed - unused */
#define ULONG_MAX	(~0UL)
#define ULLONG_MAX	(~0ULL)

#define SIZE_MAX	(~(size_t)0)
#define PHYS_ADDR_MAX	(~(phys_addr_t)0)

/* U8_MAX, U16_MAX removed - unused */
#define U32_MAX		((u32)~0U)
/* S32_MIN removed - unused */
#define U64_MAX		((u64)~0ULL)

#endif  
