#ifndef _UAPI_LINUX_SWAB_H
#define _UAPI_LINUX_SWAB_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/bitsperlong.h>

/* constant-fold swab macros removed - 0-ref (builtins used directly below) */
/* swahw32, swahb32 variants - unused */
/* __fswab16/32/64 + non-builtin __swab fallbacks dropped - builtins always present */

#define __swab32(x) (__u32)__builtin_bswap32((__u32)(x))

/* 32-bit only kernel */
static __always_inline unsigned long __swab(const unsigned long y) {
	return __swab32(y); }

/* __swab{16,32,64}{p,s} helpers removed - 0-ref (byteorder p/s aliases gone) */

#endif
