/* STUB: string helper functions - minimal implementations */

#include <linux/ctype.h>
#include <linux/string.h>


ssize_t strscpy_pad(char *dest, const char *src, size_t count) {
	size_t len;
	if (!dest || !src || !count)
		return -E2BIG;
	len = strlcpy(dest, src, count);
	if (len >= count)
		return -E2BIG;
	memset(dest + len, 0, count - len);
	return len; }


char *strreplace(char *s, char old, char new) {
	for (; *s; ++s)
		if (*s == old)
			*s = new;
	return s; }
