
#include <linux/math64.h>
#include <linux/kernel.h>
#include <linux/log2.h>

/*
 * 64/64 division on x86_32 from do_div's 64/32 step: exact when the divisor
 * fits in 32 bits, otherwise divide both by 2^n so the divisor does, which
 * gives the quotient or one more; correct with one multiply.
 */
u64 div64_u64(u64 dividend, u64 divisor)
{
	u32 high = divisor >> 32;
	u64 quot;

	if (high == 0) {
		quot = dividend;
		do_div(quot, (u32)divisor);
	} else {
		int n = fls(high);

		quot = dividend >> n;
		do_div(quot, (u32)(divisor >> n));
		if (quot != 0)
			quot--;
		if ((dividend - quot * divisor) >= divisor)
			quot++;
	}
	return quot;
}
