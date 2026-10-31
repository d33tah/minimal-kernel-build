#ifndef __LINUX_BITS_H
#define __LINUX_BITS_H

#define BITS_PER_BYTE		8

#define GENMASK_INPUT_CHECK(h, l) 	(BUILD_BUG_ON_ZERO(__builtin_choose_expr( 		__is_constexpr((l) > (h)), (l) > (h), 0)))

#define __GENMASK(h, l) 	(((~UL(0)) - (UL(1) << (l)) + 1) & 	 (~UL(0) >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK(h, l) 	(GENMASK_INPUT_CHECK(h, l) + __GENMASK(h, l))

#define __GENMASK_ULL(h, l) 	(((~ULL(0)) - (ULL(1) << (l)) + 1) & 	 (~ULL(0) >> (BITS_PER_LONG_LONG - 1 - (h))))
#define GENMASK_ULL(h, l) 	(GENMASK_INPUT_CHECK(h, l) + __GENMASK_ULL(h, l))

#endif	 
