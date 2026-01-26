#include <linux/kernel.h>
unsigned long gcd(unsigned long a, unsigned long b) __attribute_const__;
#include <linux/export.h>

/* CONFIG_CPU_NO_EFFICIENT_FFS is not set - using efficient __ffs version */
unsigned long gcd(unsigned long a, unsigned long b)
{
	unsigned long r = a | b;

	if (!a || !b)
		return r;

	b >>= __ffs(b);
	if (b == 1)
		return r & -r;

	for (;;) {
		a >>= __ffs(a);
		if (a == 1)
			return r & -r;
		if (a == b)
			return a << __ffs(r);

		if (a < b)
			swap(a, b);
		a -= b;
	}
}

/* lcm() merged from lcm.c */
unsigned long lcm(unsigned long a, unsigned long b)
{
	return (a && b) ? (a / gcd(a, b)) * b : 0;
}
/* lcm_not_zero removed - never called */

/* int_sqrt() merged from int_sqrt.c */
#include <linux/bitops.h>
unsigned long int_sqrt(unsigned long x)
{
	unsigned long b, m, y = 0;
	if (x <= 1)
		return x;
	m = 1UL << (__fls(x) & ~1UL);
	while (m != 0) {
		b = y + m;
		y >>= 1;
		if (x >= b) {
			x -= b;
			y += m;
		}
		m >>= 2;
	}
	return y;
}

/* reciprocal_value merged from reciprocal_div.c */
#include <linux/bug.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/reciprocal_div.h>
struct reciprocal_value reciprocal_value(u32 d)
{
	struct reciprocal_value R;
	u64 m;
	int l;
	l = fls(d - 1);
	m = ((1ULL << 32) * ((1ULL << l) - d));
	do_div(m, d);
	++m;
	R.m = (u32)m;
	R.sh1 = min(l, 1);
	R.sh2 = max(l - 1, 0);
	return R;
}
/* reciprocal_value_adv removed - never called (~27 LOC) */
