// SPDX-License-Identifier: GPL-2.0
/*
 * Most of the string-functions are rather heavily hand-optimized,
 * see especially strsep,strstr,str[c]spn. They should work, but are not
 * very easy to understand. Everything is done entirely within the register
 * set, making the functions fast and clean. String instructions have been
 * used through-out, making for "slightly" unclear code :-)
 *
 * AK: On P4 and K7 using non string instruction implementations might be faster
 * for large memory blocks. But most of them are unlikely to be used on large
 * strings.
 */

#include <linux/string.h>

int strcmp(const char *cs, const char *ct)
{
	int d0, d1;
	int res;
	asm volatile("1:\tlodsb\n\t"
		     "scasb\n\t"
		     "jne 2f\n\t"
		     "testb %%al,%%al\n\t"
		     "jne 1b\n\t"
		     "xorl %%eax,%%eax\n\t"
		     "jmp 3f\n"
		     "2:\tsbbl %%eax,%%eax\n\t"
		     "orb $1,%%al\n"
		     "3:"
		     : "=a"(res), "=&S"(d0), "=&D"(d1)
		     : "1"(cs), "2"(ct)
		     : "memory");
	return res;
}

size_t strlen(const char *s)
{
	int d0;
	size_t res;
	asm volatile("repne\n\t"
		     "scasb"
		     : "=c"(res), "=&D"(d0)
		     : "1"(s), "a"(0), "0"(0xffffffffu)
		     : "memory");
	return ~res - 1;
}

size_t strnlen(const char *s, size_t count)
{
	int d0, res;
	asm volatile("movl %2,%0\n\t"
		     "jmp 2f\n"
		     "1:\tcmpb $0,(%0)\n\t"
		     "je 3f\n\t"
		     "incl %0\n"
		     "2:\tdecl %1\n\t"
		     "cmpl $-1,%1\n\t"
		     "jne 1b\n"
		     "3:\tsubl %2,%0"
		     : "=a"(res), "=&d"(d0)
		     : "c"(s), "1"(count)
		     : "memory");
	return res;
}
