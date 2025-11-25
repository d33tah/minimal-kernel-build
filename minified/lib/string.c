 
 

 

#define __NO_FORTIFY
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <asm/word-at-a-time.h>
#include <asm/page.h>

#ifndef __HAVE_ARCH_STRNCASECMP
 
int strncasecmp(const char *s1, const char *s2, size_t len)
{
	 
	unsigned char c1, c2;

	if (!len)
		return 0;

	do {
		c1 = *s1++;
		c2 = *s2++;
		if (!c1 || !c2)
			break;
		if (c1 == c2)
			continue;
		c1 = tolower(c1);
		c2 = tolower(c2);
		if (c1 != c2)
			break;
	} while (--len);
	return (int)c1 - (int)c2;
}
#endif

#ifndef __HAVE_ARCH_STRCASECMP
int strcasecmp(const char *s1, const char *s2)
{
	int c1, c2;

	do {
		c1 = tolower(*s1++);
		c2 = tolower(*s2++);
	} while (c1 == c2 && c1 != 0);
	return c1 - c2;
}
#endif

#ifndef __HAVE_ARCH_STRCPY
 
char *strcpy(char *dest, const char *src)
{
	char *tmp = dest;

	while ((*dest++ = *src++) != '\0')
		 ;
	return tmp;
}
#endif

#ifndef __HAVE_ARCH_STRNCPY
 
char *strncpy(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	while (count) {
		if ((*tmp = *src) != 0)
			src++;
		tmp++;
		count--;
	}
	return dest;
}
#endif

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

	if (count == 0 || WARN_ON_ONCE(count > INT_MAX))
		return -E2BIG;

	 
	if ((long)src & (sizeof(long) - 1)) {
		size_t limit = PAGE_SIZE - ((long)src & (PAGE_SIZE - 1));
		if (limit < max)
			max = limit;
	}

	while (max >= sizeof(unsigned long)) {
		unsigned long c, data;

		c = read_word_at_a_time(src+res);
		if (has_zero(c, &data, &constants)) {
			data = prep_zero_mask(c, data, &constants);
			data = create_zero_mask(data);
			*(unsigned long *)(dest+res) = c & zero_bytemask(data);
			return res + find_zero(data);
		}
		*(unsigned long *)(dest+res) = c;
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
		dest[res-1] = '\0';

	return -E2BIG;
}
#endif

 
char *stpcpy(char *__restrict__ dest, const char *__restrict__ src);
char *stpcpy(char *__restrict__ dest, const char *__restrict__ src)
{
	while ((*dest++ = *src++) != '\0')
		 ;
	return --dest;
}

#ifndef __HAVE_ARCH_STRCAT
 
char *strcat(char *dest, const char *src)
{
	char *tmp = dest;

	while (*dest)
		dest++;
	while ((*dest++ = *src++) != '\0')
		;
	return tmp;
}
#endif

#ifndef __HAVE_ARCH_STRNCAT
 
char *strncat(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	if (count) {
		while (*dest)
			dest++;
		while ((*dest++ = *src++) != 0) {
			if (--count == 0) {
				*dest = '\0';
				break;
			}
		}
	}
	return tmp;
}
#endif

#ifndef __HAVE_ARCH_STRLCAT
 
size_t strlcat(char *dest, const char *src, size_t count)
{
	size_t dsize = strlen(dest);
	size_t len = strlen(src);
	size_t res = dsize + len;

	 
	BUG_ON(dsize >= count);

	dest += dsize;
	count -= dsize;
	if (len >= count)
		len = count-1;
	memcpy(dest, src, len);
	dest[len] = 0;
	return res;
}
#endif

#ifndef __HAVE_ARCH_STRCMP
 
int strcmp(const char *cs, const char *ct)
{
	unsigned char c1, c2;

	while (1) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
	}
	return 0;
}
#endif

#ifndef __HAVE_ARCH_STRNCMP
 
int strncmp(const char *cs, const char *ct, size_t count)
{
	unsigned char c1, c2;

	while (count) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
		count--;
	}
	return 0;
}
#endif

#ifndef __HAVE_ARCH_STRCHR
 
char *strchr(const char *s, int c)
{
	for (; *s != (char)c; ++s)
		if (*s == '\0')
			return NULL;
	return (char *)s;
}
#endif

#ifndef __HAVE_ARCH_STRCHRNUL
 
char *strchrnul(const char *s, int c)
{
	while (*s && *s != (char)c)
		s++;
	return (char *)s;
}
#endif

 
char *strnchrnul(const char *s, size_t count, int c)
{
	while (count-- && *s && *s != (char)c)
		s++;
	return (char *)s;
}

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

#ifndef __HAVE_ARCH_STRNCHR
 
char *strnchr(const char *s, size_t count, int c)
{
	while (count--) {
		if (*s == (char)c)
			return (char *)s;
		if (*s++ == '\0')
			break;
	}
	return NULL;
}
#endif

#ifndef __HAVE_ARCH_STRLEN
 
size_t strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		 ;
	return sc - s;
}
#endif

#ifndef __HAVE_ARCH_STRNLEN
 
size_t strnlen(const char *s, size_t count)
{
	const char *sc;

	for (sc = s; count-- && *sc != '\0'; ++sc)
		 ;
	return sc - s;
}
#endif

#ifndef __HAVE_ARCH_STRSPN
 
size_t strspn(const char *s, const char *accept)
{
	const char *p;

	for (p = s; *p != '\0'; ++p) {
		if (!strchr(accept, *p))
			break;
	}
	return p - s;
}
#endif

#ifndef __HAVE_ARCH_STRCSPN
 
size_t strcspn(const char *s, const char *reject)
{
	const char *p;

	for (p = s; *p != '\0'; ++p) {
		if (strchr(reject, *p))
			break;
	}
	return p - s;
}
#endif

#ifndef __HAVE_ARCH_STRPBRK
 
char *strpbrk(const char *cs, const char *ct)
{
	const char *sc1, *sc2;

	for (sc1 = cs; *sc1 != '\0'; ++sc1) {
		for (sc2 = ct; *sc2 != '\0'; ++sc2) {
			if (*sc1 == *sc2)
				return (char *)sc1;
		}
	}
	return NULL;
}
#endif

#ifndef __HAVE_ARCH_STRSEP
 
char *strsep(char **s, const char *ct)
{
	char *sbegin = *s;
	char *end;

	if (sbegin == NULL)
		return NULL;

	end = strpbrk(sbegin, ct);
	if (end)
		*end++ = '\0';
	*s = end;
	return sbegin;
}
#endif

#ifndef __HAVE_ARCH_MEMSET
 
void *memset(void *s, int c, size_t count)
{
	char *xs = s;

	while (count--)
		*xs++ = c;
	return s;
}
#endif

#ifndef __HAVE_ARCH_MEMSET16
/* Stub: memset16 not used in minimal kernel */
void *memset16(uint16_t *s, uint16_t v, size_t count) { return s; }
#endif

#ifndef __HAVE_ARCH_MEMSET32
/* Stub: memset32 not used in minimal kernel */
void *memset32(uint32_t *s, uint32_t v, size_t count) { return s; }
#endif

#ifndef __HAVE_ARCH_MEMSET64
/* Stub: memset64 not used in minimal kernel */
void *memset64(uint64_t *s, uint64_t v, size_t count) { return s; }
#endif

#ifndef __HAVE_ARCH_MEMCPY
 
void *memcpy(void *dest, const void *src, size_t count)
{
	char *tmp = dest;
	const char *s = src;

	while (count--)
		*tmp++ = *s++;
	return dest;
}
#endif

#ifndef __HAVE_ARCH_MEMMOVE
 
void *memmove(void *dest, const void *src, size_t count)
{
	char *tmp;
	const char *s;

	if (dest <= src) {
		tmp = dest;
		s = src;
		while (count--)
			*tmp++ = *s++;
	} else {
		tmp = dest;
		tmp += count;
		s = src;
		s += count;
		while (count--)
			*--tmp = *--s;
	}
	return dest;
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

/* Stub: bcmp not used in minimal kernel (except boot/kconfig which use their own) */
int bcmp(const void *a, const void *b, size_t len) { return 0; }

#ifndef __HAVE_ARCH_MEMSCAN
/* Stubbed - not used externally */
void *memscan(void *addr, int c, size_t size)
{
	return addr;
}
#endif

#ifndef __HAVE_ARCH_STRSTR
 
char *strstr(const char *s1, const char *s2)
{
	size_t l1, l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	l1 = strlen(s1);
	while (l1 >= l2) {
		l1--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}
#endif

#ifndef __HAVE_ARCH_STRNSTR
/* Stubbed - not used externally */
char *strnstr(const char *s1, const char *s2, size_t len)
{
	return NULL;
}
#endif

#ifndef __HAVE_ARCH_MEMCHR
 
void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = s;
	while (n-- != 0) {
        	if ((unsigned char)c == *p++) {
			return (void *)(p - 1);
		}
	}
	return NULL;
}
#endif

static void *check_bytes8(const u8 *start, u8 value, unsigned int bytes)
{
	while (bytes) {
		if (*start != value)
			return (void *)start;
		start++;
		bytes--;
	}
	return NULL;
}

/* Stub: memchr_inv not used in minimal kernel */
void *memchr_inv(const void *start, int c, size_t bytes)
{
	return NULL;
}
