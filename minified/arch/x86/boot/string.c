

#include <linux/types.h>
#include <asm/asm.h>

#undef memcpy
#undef memset
#undef memcmp

int memcmp(const void *s1, const void *s2, size_t len)
{
	bool diff;
	asm("repe; cmpsb" CC_SET(nz)
	    : CC_OUT(nz)(diff), "+D"(s1), "+S"(s2), "+c"(len));
	return diff;
}

int strcmp(const char *str1, const char *str2)
{
	const unsigned char *s1 = (const unsigned char *)str1;
	const unsigned char *s2 = (const unsigned char *)str2;
	int delta = 0;

	while (*s1 || *s2) {
		delta = *s1 - *s2;
		if (delta)
			return delta;
		s1++;
		s2++;
	}
	return 0;
}

size_t strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		;
	return sc - s;
}
