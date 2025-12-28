/* BCD conversion functions */
#include <linux/compiler.h>
unsigned _bcd2bin(unsigned char val) __attribute_const__;
unsigned _bcd2bin(unsigned char val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}
