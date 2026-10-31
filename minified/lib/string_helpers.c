/* STUB: string helper functions - minimal implementations */

#include <linux/string.h>

ssize_t strscpy_pad(char *dest, const char *src, size_t count)
{
	size_t len;
	if (!dest || !src || !count)
		return -E2BIG;
	len = strlcpy(dest, src, count);
	if (len >= count)
		return -E2BIG;
	memset(dest + len, 0, count - len);
	return len;
}
