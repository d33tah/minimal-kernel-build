/* Hash functions for i386 (BITS_PER_LONG == 32) */
#ifndef _LINUX_HASH_H
#define _LINUX_HASH_H

#include <asm/types.h>
#include <linux/compiler.h>

#define GOLDEN_RATIO_32 0x61C88647
#define GOLDEN_RATIO_PRIME GOLDEN_RATIO_32
#define hash_long(val, bits) hash_32(val, bits)

#ifndef HAVE_ARCH__HASH_32
#define __hash_32 __hash_32_generic
#endif
static inline u32 __hash_32_generic(u32 val)
{
	return val * GOLDEN_RATIO_32;
}

static inline u32 hash_32(u32 val, unsigned int bits)
{
	return __hash_32(val) >> (32 - bits);
}

#ifndef HAVE_ARCH_HASH_64
#define hash_64 hash_64_generic
#endif
static __always_inline u32 hash_64_generic(u64 val, unsigned int bits)
{
	return hash_32((u32)val ^ __hash_32(val >> 32), bits);
}

static inline u32 hash_ptr(const void *ptr, unsigned int bits)
{
	return hash_long((unsigned long)ptr, bits);
}

static inline u32 hash32_ptr(const void *ptr)
{
	return (u32)(unsigned long)ptr;
}

#endif
