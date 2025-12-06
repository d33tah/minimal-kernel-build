#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

/* Inlined from uapi/linux/limits.h */
#define NR_OPEN	        1024
#define NGROUPS_MAX    65536
#define ARG_MAX       131072
#define LINK_MAX         127
#define MAX_CANON        255
#define MAX_INPUT        255
#define NAME_MAX         255
#define PATH_MAX        4096
#define PIPE_BUF        4096
#define XATTR_NAME_MAX   255
#define XATTR_SIZE_MAX 65536
#define XATTR_LIST_MAX 65536
#define RTSIG_MAX	  32

#include <linux/types.h>
#include <vdso/limits.h>

#define SIZE_MAX	(~(size_t)0)
#define PHYS_ADDR_MAX	(~(phys_addr_t)0)

#define U8_MAX		((u8)~0U)
#define S8_MAX		((s8)(U8_MAX >> 1))
#define S8_MIN		((s8)(-S8_MAX - 1))
#define U16_MAX		((u16)~0U)
#define S16_MAX		((s16)(U16_MAX >> 1))
#define S16_MIN		((s16)(-S16_MAX - 1))
#define U32_MAX		((u32)~0U)
#define U32_MIN		((u32)0)
#define S32_MAX		((s32)(U32_MAX >> 1))
#define S32_MIN		((s32)(-S32_MAX - 1))
#define U64_MAX		((u64)~0ULL)
#define S64_MAX		((s64)(U64_MAX >> 1))
#define S64_MIN		((s64)(-S64_MAX - 1))

#endif  
