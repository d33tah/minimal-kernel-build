#ifndef _UAPI_LINUX_SWAB_H
#define _UAPI_LINUX_SWAB_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/bitsperlong.h>
#include <asm/swab.h>

#define ___constant_swab16(x) ((__u16)(				\
	(((__u16)(x) & (__u16)0x00ffU) << 8) |			\
	(((__u16)(x) & (__u16)0xff00U) >> 8)))

#define ___constant_swab32(x) ((__u32)(				\
	(((__u32)(x) & (__u32)0x000000ffUL) << 24) |		\
	(((__u32)(x) & (__u32)0x0000ff00UL) <<  8) |		\
	(((__u32)(x) & (__u32)0x00ff0000UL) >>  8) |		\
	(((__u32)(x) & (__u32)0xff000000UL) >> 24)))

#define ___constant_swab64(x) ((__u64)(				\
	(((__u64)(x) & (__u64)0x00000000000000ffULL) << 56) |	\
	(((__u64)(x) & (__u64)0x000000000000ff00ULL) << 40) |	\
	(((__u64)(x) & (__u64)0x0000000000ff0000ULL) << 24) |	\
	(((__u64)(x) & (__u64)0x00000000ff000000ULL) <<  8) |	\
	(((__u64)(x) & (__u64)0x000000ff00000000ULL) >>  8) |	\
	(((__u64)(x) & (__u64)0x0000ff0000000000ULL) >> 24) |	\
	(((__u64)(x) & (__u64)0x00ff000000000000ULL) >> 40) |	\
	(((__u64)(x) & (__u64)0xff00000000000000ULL) >> 56)))

/* ___constant_swahw32, ___constant_swahb32 - unused */


static inline __attribute_const__ __u16 __fswab16(__u16 val)
{
#if defined (__arch_swab16)
	return __arch_swab16(val);
#else
	return ___constant_swab16(val);
#endif
}

static inline __attribute_const__ __u32 __fswab32(__u32 val)
{
#if defined(__arch_swab32)
	return __arch_swab32(val);
#else
	return ___constant_swab32(val);
#endif
}

static inline __attribute_const__ __u64 __fswab64(__u64 val)
{
#if defined (__arch_swab64)
	return __arch_swab64(val);
#elif defined(__SWAB_64_THRU_32__)
	__u32 h = val >> 32;
	__u32 l = val & ((1ULL << 32) - 1);
	return (((__u64)__fswab32(l)) << 32) | ((__u64)(__fswab32(h)));
#else
	return ___constant_swab64(val);
#endif
}

/* __fswahw32, __fswahb32 - unused */

#ifdef __HAVE_BUILTIN_BSWAP16__
#define __swab16(x) (__u16)__builtin_bswap16((__u16)(x))
#else
#define __swab16(x)				\
	(__builtin_constant_p((__u16)(x)) ?	\
	___constant_swab16(x) :			\
	__fswab16(x))
#endif

#ifdef __HAVE_BUILTIN_BSWAP32__
#define __swab32(x) (__u32)__builtin_bswap32((__u32)(x))
#else
#define __swab32(x)				\
	(__builtin_constant_p((__u32)(x)) ?	\
	___constant_swab32(x) :			\
	__fswab32(x))
#endif

#ifdef __HAVE_BUILTIN_BSWAP64__
#define __swab64(x) (__u64)__builtin_bswap64((__u64)(x))
#else
#define __swab64(x)				\
	(__builtin_constant_p((__u64)(x)) ?	\
	___constant_swab64(x) :			\
	__fswab64(x))
#endif

/* 32-bit only kernel */
static __always_inline unsigned long __swab(const unsigned long y)
{
	return __swab32(y);
}

/* __swab*p, __swab*s functions removed - unused */


#endif  
