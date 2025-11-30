 
#ifndef _LINUX_MINMAX_H
#define _LINUX_MINMAX_H

#include <linux/const.h>

 
#define __typecheck(x, y) \
	(!!(sizeof((typeof(x) *)1 == (typeof(y) *)1)))

#define __no_side_effects(x, y) \
		(__is_constexpr(x) && __is_constexpr(y))

#define __safe_cmp(x, y) \
		(__typecheck(x, y) && __no_side_effects(x, y))

#define __cmp(x, y, op)	((x) op (y) ? (x) : (y))

#define __cmp_once(x, y, unique_x, unique_y, op) ({	\
		typeof(x) unique_x = (x);		\
		typeof(y) unique_y = (y);		\
		__cmp(unique_x, unique_y, op); })

#define __careful_cmp(x, y, op) \
	__builtin_choose_expr(__safe_cmp(x, y), \
		__cmp(x, y, op), \
		__cmp_once(x, y, __UNIQUE_ID(__x), __UNIQUE_ID(__y), op))

 
#define min(x, y)	__careful_cmp(x, y, <)

 
#define max(x, y)	__careful_cmp(x, y, >)

 
#define min3(x, y, z) min((typeof(x))min(x, y), z)

 
#define max3(x, y, z) max((typeof(x))max(x, y), z)

 
#define min_not_zero(x, y) ({			\
	typeof(x) __x = (x);			\
	typeof(y) __y = (y);			\
	__x == 0 ? __y : ((__y == 0) ? __x : min(__x, __y)); })

 
#define clamp(val, lo, hi) min((typeof(val))max(val, lo), hi)

 

 
#define min_t(type, x, y)	__careful_cmp((type)(x), (type)(y), <)

 
#define max_t(type, x, y)	__careful_cmp((type)(x), (type)(y), >)

 
#define clamp_t(type, val, lo, hi) min_t(type, max_t(type, val, lo), hi)

 
#define clamp_val(val, lo, hi) clamp_t(typeof(val), val, lo, hi)

 
#define swap(a, b) \
	do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

#endif	 
