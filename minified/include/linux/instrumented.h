
#ifndef _LINUX_INSTRUMENTED_H
#define _LINUX_INSTRUMENTED_H

#include <linux/compiler.h>
/* kasan_check_read/write provided by asm-generic/rwonce.h */
#include <linux/types.h>

static __always_inline void instrument_write(const volatile void *v, size_t size)
{
	kasan_check_write(v, size);
}

static __always_inline void instrument_read_write(const volatile void *v, size_t size)
{
	kasan_check_write(v, size);
}

static __always_inline void instrument_atomic_read(const volatile void *v, size_t size)
{
	kasan_check_read(v, size);
}

static __always_inline void instrument_atomic_write(const volatile void *v, size_t size)
{
	kasan_check_write(v, size);
}

static __always_inline void instrument_atomic_read_write(const volatile void *v, size_t size)
{
	kasan_check_write(v, size);
}

static __always_inline void
instrument_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	kasan_check_read(from, n);
}

/* instrument_copy_from_user removed - never called */

#endif  
