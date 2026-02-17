#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/stdarg.h>

#define __HAVE_ARCH_STRCPY
extern char *strcpy(char *dest, const char *src);

#define __HAVE_ARCH_STRNCPY
extern char *strncpy(char *dest, const char *src, size_t count);

#define __HAVE_ARCH_STRCMP
extern int strcmp(const char *cs, const char *ct);

#define __HAVE_ARCH_STRNCMP
extern int strncmp(const char *cs, const char *ct, size_t count);

#define __HAVE_ARCH_STRCHR
extern char *strchr(const char *s, int c);

#define __HAVE_ARCH_STRLEN
extern size_t strlen(const char *s);

static __always_inline void *__memcpy(void *to, const void *from, size_t n)
{
	int d0, d1, d2;
	asm volatile("rep ; movsl\n\t"
		     "movl %4,%%ecx\n\t"
		     "andl $3,%%ecx\n\t"
		     "jz 1f\n\t"
		     "rep ; movsb\n\t"
		     "1:"
		     : "=&c" (d0), "=&D" (d1), "=&S" (d2)
		     : "0" (n / 4), "g" (n), "1" ((long)to), "2" ((long)from)
		     : "memory");
	return to;
}

extern void *memcpy(void *, const void *, size_t);
#define memcpy(t, f, n) __builtin_memcpy(t, f, n)

void *memmove(void *dest, const void *src, size_t n);

extern int memcmp(const void *, const void *, size_t);
#define memcmp __builtin_memcmp

static inline void *__memset_generic(void *s, char c, size_t count)
{
	int d0, d1;
	asm volatile("rep\n\t"
		     "stosb"
		     : "=&c" (d0), "=&D" (d1)
		     : "a" (c), "1" (s), "0" (count)
		     : "memory");
	return s;
}

#define __constant_count_memset(s, c, count) __memset_generic((s), (c), (count))

#define __HAVE_ARCH_STRNLEN
extern size_t strnlen(const char *s, size_t count);

extern char *strstr(const char *cs, const char *ct);

#define __memset(s, c, count)				\
	(__builtin_constant_p(count)			\
	 ? __constant_count_memset((s), (c), (count))	\
	 : __memset_generic((s), (c), (count)))

extern void *memset(void *, int, size_t);
#define memset(s, c, count) __builtin_memset(s, c, count)

static inline void *memset16(uint16_t *s, uint16_t v, size_t n)
{
	int d0, d1;
	asm volatile("rep\n\t"
		     "stosw"
		     : "=&c" (d0), "=&D" (d1)
		     : "a" (v), "1" (s), "0" (n)
		     : "memory");
	return s;
}
/* end string_32.h */

#ifndef __HAVE_ARCH_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif
#ifndef __HAVE_ARCH_STRSCPY
ssize_t strscpy(char *, const char *, size_t);
#endif

#ifndef __HAVE_ARCH_STRLCAT
extern size_t strlcat(char *, const char *, __kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRNCHR
extern char * strnchr(const char *, size_t, int);
#endif
#ifndef __HAVE_ARCH_STRRCHR
extern char * strrchr(const char *,int);
#endif

#ifndef __HAVE_ARCH_STRPBRK
extern char * strpbrk(const char *,const char *);
#endif
#ifndef __HAVE_ARCH_STRSEP
#endif

extern void kfree_const(const void *x);
extern const char *kstrdup_const(const char *s, gfp_t gfp);
extern char *kmemdup_nul(const char *s, size_t len, gfp_t gfp);

#endif
