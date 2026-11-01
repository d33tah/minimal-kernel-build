/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_STRING_32_H
#define _ASM_X86_STRING_32_H

extern int strcmp(const char *cs, const char *ct);

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
		     : "=&c"(d0), "=&D"(d1), "=&S"(d2)
		     : "0"(n / 4), "g"(n), "1"((long)to), "2"((long)from)
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
		     : "=&c"(d0), "=&D"(d1)
		     : "a"(c), "1"(s), "0"(count)
		     : "memory");
	return s;
}

/* we might want to write optimized versions of these later */
#define __constant_count_memset(s, c, count) __memset_generic((s), (c), (count))

extern size_t strnlen(const char *s, size_t count);

#define __memset(s, c, count)                                 \
	(__builtin_constant_p(count) ?                        \
		 __constant_count_memset((s), (c), (count)) : \
		 __memset_generic((s), (c), (count)))

extern void *memset(void *, int, size_t);
#define memset(s, c, count) __builtin_memset(s, c, count)

static inline void *memset16(uint16_t *s, uint16_t v, size_t n)
{
	int d0, d1;
	asm volatile("rep\n\t"
		     "stosw"
		     : "=&c"(d0), "=&D"(d1)
		     : "a"(v), "1"(s), "0"(n)
		     : "memory");
	return s;
}

#endif
