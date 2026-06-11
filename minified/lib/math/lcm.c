#include <linux/compiler.h>
unsigned long gcd(unsigned long a, unsigned long b) __attribute_const__;
#include <linux/export.h>
/* lcm.h - functions defined below */

unsigned long lcm(unsigned long a, unsigned long b)
{
	if (a && b)
		return (a / gcd(a, b)) * b;
	else
		return 0;
}
