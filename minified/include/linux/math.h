#ifndef _LINUX_MATH_H
#define _LINUX_MATH_H

#include <linux/types.h>
#include <asm/div64.h>

#define __round_mask(x, y) ((__typeof__(x))((y) - 1))

#define round_up(x, y) ((((x) - 1) | __round_mask(x, y)) + 1)

#define round_down(x, y) ((x) & ~__round_mask(x, y))

#define DIV_ROUND_UP __KERNEL_DIV_ROUND_UP

#define __abs_choose_expr(x, type, other)                               \
	__builtin_choose_expr(                                          \
		__builtin_types_compatible_p(typeof(x), signed type) || \
			__builtin_types_compatible_p(typeof(x),         \
						     unsigned type),    \
		({                                                      \
			signed type __x = (x);                          \
			__x < 0 ? -__x : __x;                           \
		}),                                                     \
		other)

#endif
