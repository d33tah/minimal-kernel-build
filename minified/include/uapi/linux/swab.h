#ifndef _UAPI_LINUX_SWAB_H
#define _UAPI_LINUX_SWAB_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/bitsperlong.h>

/* constant-fold swab macros removed - 0-ref (builtins used directly below) */
/* swahw32, swahb32 variants - unused */
/* __fswab16/32/64 + non-builtin __swab fallbacks dropped - builtins always present */

#define __swab16(x) (__u16)__builtin_bswap16((__u16)(x))
#define __swab32(x) (__u32)__builtin_bswap32((__u32)(x))
#define __swab64(x) (__u64)__builtin_bswap64((__u64)(x))

/* 32-bit only kernel */
static __always_inline unsigned long __swab(const unsigned long y)
{
	return __swab32(y);
}

/* __swahw32, __swahb32 macros - unused */

static __always_inline __u16 __swab16p(const __u16 *p)
{
	return __swab16(*p);
}

static __always_inline __u32 __swab32p(const __u32 *p)
{
	return __swab32(*p);
}

static __always_inline __u64 __swab64p(const __u64 *p)
{
	return __swab64(*p);
}

/* __swahw32p, __swahb32p - unused */

static inline void __swab16s(__u16 *p)
{
	*p = __swab16p(p);
}
static __always_inline void __swab32s(__u32 *p)
{
	*p = __swab32p(p);
}

static __always_inline void __swab64s(__u64 *p)
{
	*p = __swab64p(p);
}

/* __swahw32s, __swahb32s - unused */


#endif  
