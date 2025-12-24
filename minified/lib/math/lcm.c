#include <linux/compiler.h>
unsigned long gcd(unsigned long a, unsigned long b) __attribute_const__;
#include <linux/export.h>
unsigned long lcm(unsigned long a, unsigned long b)
{
	return (a && b) ? (a / gcd(a, b)) * b : 0;
}
unsigned long lcm_not_zero(unsigned long a, unsigned long b)
{
	unsigned long l = lcm(a, b);
	return l ? l : (b ? b : a);
}
