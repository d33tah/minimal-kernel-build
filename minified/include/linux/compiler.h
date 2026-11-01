/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

#ifndef __ASSEMBLY__

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* The "volatile" is due to gcc bugs */
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

/**
 * data_race - mark an expression as containing intentional data races
 *
 * This data_race() macro is useful for situations in which data races
 * should be forgiven.  One example is diagnostic code that accesses
 * shared variables but is not a part of the core synchronization design.
 *
 * This macro *does not* affect normal code generation, but is a hint
 * to tooling that data races here are to be ignored.
 */
#define data_race(expr)                                                \
	({                                                             \
		__unqual_scalar_typeof(({ expr; })) __v = ({ expr; }); \
		__v;                                                   \
	})

/*
 * Force the compiler to emit 'sym' as a symbol, so that we can reference
 * it from inline assembler. Necessary in case 'sym' could be inlined
 * otherwise, or eliminated entirely due to lack of references that are
 * visible to the compiler.
 */
#define __ADDRESSABLE(sym)                                         \
	static void *__section(".discard.addressable")             \
		__used __UNIQUE_ID(__PASTE(__addressable_, sym)) = \
			(void *)&sym;

/**
 * offset_to_ptr - convert a relative memory offset to an absolute pointer
 * @off:	the address of the 32-bit offset value
 */
static inline void *offset_to_ptr(const int *off)
{
	return (void *)((unsigned long)off + *off);
}

#endif /* __ASSEMBLY__ */

/* &a[0] degrades to a pointer: a different type from an array */
#define __must_be_array(a) BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

#include <asm/rwonce.h>

#endif /* __LINUX_COMPILER_H */
