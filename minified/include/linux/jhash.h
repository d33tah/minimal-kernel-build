#ifndef _LINUX_JHASH_H
#define _LINUX_JHASH_H

#include <linux/bitops.h>
#include <linux/unaligned/packed_struct.h>

#define jhash_size(n)   ((u32)1<<(n))
#define jhash_mask(n)   (jhash_size(n)-1)

#define __jhash_mix(a, b, c)			\
{						\
	a -= c;  a ^= rol32(c, 4);  c += b;	\
	b -= a;  b ^= rol32(a, 6);  a += c;	\
	c -= b;  c ^= rol32(b, 8);  b += a;	\
	a -= c;  a ^= rol32(c, 16); c += b;	\
	b -= a;  b ^= rol32(a, 19); a += c;	\
	c -= b;  c ^= rol32(b, 4);  b += a;	\
}

#define __jhash_final(a, b, c)			\
{						\
	c ^= b; c -= rol32(b, 14);		\
	a ^= c; a -= rol32(c, 11);		\
	b ^= a; b -= rol32(a, 25);		\
	c ^= b; c -= rol32(b, 16);		\
	a ^= c; a -= rol32(c, 4);		\
	b ^= a; b -= rol32(a, 14);		\
	c ^= b; c -= rol32(b, 24);		\
}

#define JHASH_INITVAL		0xdeadbeef

static inline u32 jhash(const void *key, u32 length, u32 initval)
{
	u32 a, b, c;
	const u8 *k = key;

	 
	a = b = c = JHASH_INITVAL + length + initval;

	 
	while (length > 12) {
		a += __get_unaligned_cpu32(k);
		b += __get_unaligned_cpu32(k + 4);
		c += __get_unaligned_cpu32(k + 8);
		__jhash_mix(a, b, c);
		length -= 12;
		k += 12;
	}
	 
	switch (length) {
	case 12: c += (u32)k[11]<<24;	fallthrough;
	case 11: c += (u32)k[10]<<16;	fallthrough;
	case 10: c += (u32)k[9]<<8;	fallthrough;
	case 9:  c += k[8];		fallthrough;
	case 8:  b += (u32)k[7]<<24;	fallthrough;
	case 7:  b += (u32)k[6]<<16;	fallthrough;
	case 6:  b += (u32)k[5]<<8;	fallthrough;
	case 5:  b += k[4];		fallthrough;
	case 4:  a += (u32)k[3]<<24;	fallthrough;
	case 3:  a += (u32)k[2]<<16;	fallthrough;
	case 2:  a += (u32)k[1]<<8;	fallthrough;
	case 1:  a += k[0];
		 __jhash_final(a, b, c);
		 break;
	case 0:  
		break;
	}

	return c;
}


#endif  
