
#include <linux/bug.h>
#include <linux/errno.h>

#define __get_unaligned_t(type, ptr)                        \
	({                                                  \
		const struct {                              \
			type x;                             \
		} __packed *__pptr = (typeof(__pptr))(ptr); \
		__pptr->x;                                  \
	})
#define get_unaligned(ptr) __get_unaligned_t(typeof(*(ptr)), (ptr))
#include <asm/word-at-a-time.h>
#include <asm/page.h>

#ifndef __HAVE_ARCH_STRLCPY
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}
#endif

#ifndef __HAVE_ARCH_STRSCPY
ssize_t strscpy(char *dest, const char *src, size_t count)
{
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;
	size_t max = count;
	long res = 0;

	if (count == 0 || count > INT_MAX)
		return -E2BIG;

	if ((long)src & (sizeof(long) - 1)) {
		size_t limit = PAGE_SIZE - ((long)src & (PAGE_SIZE - 1));
		if (limit < max)
			max = limit;
	}

	while (max >= sizeof(unsigned long)) {
		unsigned long c, data;

		c = read_word_at_a_time(src + res);
		if (has_zero(c, &data, &constants)) {
			data = prep_zero_mask(c, data, &constants);
			data = create_zero_mask(data);
			*(unsigned long *)(dest + res) = c &
							 zero_bytemask(data);
			return res + find_zero(data);
		}
		*(unsigned long *)(dest + res) = c;
		res += sizeof(unsigned long);
		count -= sizeof(unsigned long);
		max -= sizeof(unsigned long);
	}

	while (count) {
		char c;

		c = src[res];
		dest[res] = c;
		if (!c)
			return res;
		res++;
		count--;
	}

	if (res)
		dest[res - 1] = '\0';

	return -E2BIG;
}
#endif

#ifndef __HAVE_ARCH_STRRCHR
char *strrchr(const char *s, int c)
{
	const char *last = NULL;
	do {
		if (*s == (char)c)
			last = s;
	} while (*s++);
	return (char *)last;
}
#endif

#ifndef __HAVE_ARCH_MEMCMP
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
#endif
