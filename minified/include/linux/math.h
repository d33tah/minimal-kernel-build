#ifndef _LINUX_MATH_H
#define _LINUX_MATH_H

#include <linux/types.h>
#include <asm/div64.h>
#include <linux/const.h>

#define __round_mask(x, y) ((__typeof__(x))((y)-1))

#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)

#define round_down(x, y) ((x) & ~__round_mask(x, y))

#define DIV_ROUND_UP __KERNEL_DIV_ROUND_UP


#define roundup(x, y) (					\
{							\
	typeof(y) __y = y;				\
	(((x) + (__y - 1)) / __y) * __y;		\
}							\
)


#endif
