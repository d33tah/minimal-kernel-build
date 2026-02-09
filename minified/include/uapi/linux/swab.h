#ifndef _UAPI_LINUX_SWAB_H
#define _UAPI_LINUX_SWAB_H

#include <linux/types.h>
#include <linux/compiler.h>

/* __arch_swab32/64, ___constant_swab16/32/64, __fswab16/32/64 removed
   - builtins always available with clang/LLVM */

#define __swab16(x) (__u16)__builtin_bswap16((__u16)(x))
#define __swab32(x) (__u32)__builtin_bswap32((__u32)(x))
#define __swab64(x) (__u64)__builtin_bswap64((__u64)(x))

/* 32-bit only kernel */
static __always_inline unsigned long __swab(const unsigned long y)
{
	return __swab32(y);
}

#endif
