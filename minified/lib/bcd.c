/* BCD conversion functions */
#include <linux/bcd.h>
unsigned _bcd2bin(unsigned char val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}
