#ifndef _BCD_H
#define _BCD_H
#include <linux/compiler.h>
#define bcd2bin(x) (__builtin_constant_p((u8 )(x)) ? const_bcd2bin(x) : _bcd2bin(x))
#define const_bcd2bin(x) (((x) & 0x0f) + ((x) >> 4) * 10)
unsigned _bcd2bin(unsigned char val) __attribute_const__;
#endif  
