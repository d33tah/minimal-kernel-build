#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

#ifndef __ASSEMBLY__

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define barrier() __asm__ __volatile__("" : : : "memory")

#define unreachable()                    \
	do {                             \
		__builtin_unreachable(); \
	} while (0)

#define RELOC_HIDE(ptr, off)                  \
	({                                    \
		unsigned long __ptr;          \
		__ptr = (unsigned long)(ptr); \
		(typeof(ptr))(__ptr + (off)); \
	})

#define absolute_pointer(val) RELOC_HIDE((void *)(val), 0)

#define data_race(expr)                                                \
	({                                                             \
		__unqual_scalar_typeof(({ expr; })) __v = ({ expr; }); \
		__v;                                                   \
	})

#define __ADDRESSABLE(sym)                                         \
	static void *__section(".discard.addressable")             \
		__used __UNIQUE_ID(__PASTE(__addressable_, sym)) = \
			(void *)&sym;

static inline void *offset_to_ptr(const int *off)
{
	return (void *)((unsigned long)off + *off);
}

#endif

#define __must_be_array(a) BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

#include <asm/rwonce.h>

#endif
