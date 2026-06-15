#include <linux/kernel.h>
unsigned long gcd(unsigned long a, unsigned long b) __attribute_const__;
#include <linux/export.h>


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

