#include <linux/string.h>

#undef memcpy
#undef memset
#undef memmove

__visible void *memcpy(void *to, const void *from, size_t n)
{
	return __memcpy(to, from, n);
}

__visible void *memset(void *s, int c, size_t count)
{
	return __memset(s, c, count);
}

__visible void *memmove(void *dest, const void *src, size_t n)
{
	char *d = dest;
	const char *s = src;

	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dest;
}
