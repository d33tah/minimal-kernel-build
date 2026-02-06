
#include <linux/bitops.h>
/* linux/export.h removed - no EXPORT_SYMBOL */
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/log2.h>

#if BITS_PER_LONG == 32

/* __div64_32 removed - x86 do_div uses inline asm, never calls this fallback */

#ifndef div64_u64
u64 div64_u64(u64 dividend, u64 divisor)
{
	u32 high = divisor >> 32;
	u64 quot;

	if (high == 0) {
		quot = div_u64(dividend, divisor);
	} else {
		int n = fls(high);
		quot = div_u64(dividend >> n, divisor >> n);

		if (quot != 0)
			quot--;
		if ((dividend - quot * divisor) >= divisor)
			quot++;
	}

	return quot;
}
#endif

/* div64_s64 removed - never called */
/* iter_div_u64_rem removed - never called (inline version used) */

#endif
