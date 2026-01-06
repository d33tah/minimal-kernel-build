#ifndef _LINUX_MATH64_H
#define _LINUX_MATH64_H

#include <linux/types.h>
#include <linux/math.h>
#include <asm/div64.h>

/* Inlined from vdso/math64.h */
static __always_inline u32
__iter_div_u64_rem(u64 dividend, u32 divisor, u64 *remainder)
{
	u32 ret = 0;

	while (dividend >= divisor) {
		asm("" : "+rm"(dividend));
		dividend -= divisor;
		ret++;
	}

	*remainder = dividend;

	return ret;
}

/* div64_long, div64_ul removed - never used */

#ifndef div_u64_rem
static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	*remainder = do_div(dividend, divisor);
	return dividend;
}
#endif

#ifndef div_s64_rem
extern s64 div_s64_rem(s64 dividend, s32 divisor, s32 *remainder);
#endif

#ifndef div64_u64_rem
extern u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder);
#endif

#ifndef div64_u64
extern u64 div64_u64(u64 dividend, u64 divisor);
#endif

#ifndef div64_s64
extern s64 div64_s64(s64 dividend, s64 divisor);
#endif

#ifndef div_u64
static inline u64 div_u64(u64 dividend, u32 divisor)
{
	u32 remainder;
	return div_u64_rem(dividend, divisor, &remainder);
}
#endif

#ifndef div_s64
static inline s64 div_s64(s64 dividend, s32 divisor)
{
	s32 remainder;
	return div_s64_rem(dividend, divisor, &remainder);
}
#endif

u32 iter_div_u64_rem(u64 dividend, u32 divisor, u64 *remainder);

#ifndef mul_u32_u32
static inline u64 mul_u32_u32(u32 a, u32 b)
{
	return (u64)a * b;
}
#endif

#ifndef mul_u64_u32_shr
static inline u64 mul_u64_u32_shr(u64 a, u32 mul, unsigned int shift)
{
	u32 ah, al;
	u64 ret;

	al = a;
	ah = a >> 32;

	ret = mul_u32_u32(al, mul) >> shift;
	if (ah)
		ret += mul_u32_u32(ah, mul) << (32 - shift);

	return ret;
}
#endif  

/* mul_u64_u64_shr removed - no callers */

u64 mul_u64_u64_div_u64(u64 a, u64 mul, u64 div);

#endif  
