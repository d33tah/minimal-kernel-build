

#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/bug.h>

#include <asm/unaligned.h>

/**
 * strscpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @count: Size of destination buffer
 *
 * Copy the string, or as much of it as fits, into the dest buffer.  The
 * behavior is undefined if the string buffers overlap.  The destination
 * buffer is always NUL terminated, unless it's zero-sized.
 *
 * Preferred to strlcpy() since the API doesn't require reading memory
 * from the src string beyond the specified "count" bytes, and since
 * the return value is easier to error-check than strlcpy()'s.
 * In addition, the implementation is robust to the string changing out
 * from underneath it, unlike the current strlcpy() implementation.
 *
 * Preferred to strncpy() since it always returns a valid string, and
 * doesn't unnecessarily force the tail of the destination buffer to be
 * zeroed.  If zeroing is desired please use strscpy_pad().
 *
 * Returns:
 * * The number of characters copied (not including the trailing %NUL)
 * * -E2BIG if count is 0 or @src was truncated.
 */
ssize_t strscpy(char *dest, const char *src, size_t count)
{
	long res = 0;

	if (count == 0 || WARN_ON_ONCE(count > INT_MAX))
		return -E2BIG;

	while (count) {
		char c;

		c = src[res];
		dest[res] = c;
		if (!c)
			return res;
		res++;
		count--;
	}

	/* Hit buffer length without finding a NUL; force NUL-termination. */
	if (res)
		dest[res - 1] = '\0';

	return -E2BIG;
}

char *strrchr(const char *s, int c)
{
	const char *last = NULL;
	do {
		if (*s == (char)c)
			last = s;
	} while (*s++);
	return (char *)last;
}

#undef memcmp
__visible int memcmp(const void *cs, const void *ct, size_t count)
{
	const unsigned char *su1, *su2;
	int res = 0;

	if (count >= sizeof(unsigned long)) {
		const unsigned long *u1 = cs;
		const unsigned long *u2 = ct;
		do {
			if (get_unaligned(u1) != get_unaligned(u2))
				break;
			u1++;
			u2++;
			count -= sizeof(unsigned long);
		} while (count >= sizeof(unsigned long));
		cs = u1;
		ct = u2;
	}
	for (su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
		if ((res = *su1 - *su2) != 0)
			break;
	return res;
}
