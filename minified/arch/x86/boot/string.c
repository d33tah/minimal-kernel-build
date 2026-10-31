

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
