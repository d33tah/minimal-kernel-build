#include <linux/kernel.h>
/* linux/export.h removed - no EXPORT_SYMBOL */

/* CONFIG_CPU_NO_EFFICIENT_FFS is not set - using efficient __ffs version */
static unsigned long gcd(unsigned long a, unsigned long b)
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
/* int_sqrt removed - never called */

/* reciprocal_value removed - never called after kmem_cache.reciprocal_size field removal */
