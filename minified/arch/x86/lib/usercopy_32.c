/* linux/export.h removed - no EXPORT_SYMBOL */
#include <linux/uaccess.h>
#include <asm/asm.h>

/* __movsl_is_ok removed - always returned 1, movsl_is_ok now constant true */
#define movsl_is_ok(a1, a2, n) 1

/* d33tah: disabled clang-format because extra spaces break inline asm */
/* clang-format off */
#define __do_clear_user(addr,size)					\
do {									\
	int __d0;							\
	__asm__ __volatile__(						\
		ASM_STAC "\n"						\
		"0:	rep; stosl\n"					\
		"	movl %2,%0\n"					\
		"1:	rep; stosb\n"					\
		"2: " ASM_CLAC "\n"					\
		_ASM_EXTABLE_TYPE_REG(0b, 2b, EX_TYPE_UCOPY_LEN4, %2)	\
		_ASM_EXTABLE_UA(1b, 2b)					\
		: "=&c"(size), "=&D" (__d0)				\
		: "r"(size & 3), "0"(size / 4), "1"(addr), "a"(0));	\
} while (0)
/* clang-format on */

unsigned long clear_user(void __user *to, unsigned long n)
{
	if (access_ok(to, n))
		__do_clear_user(to, n);
	return n;
}

/* __copy_user_intel declaration removed - never called (movsl_is_ok always 1) */

/* d33tah: disabled clang-format because extra spaces break inline asm */
/* clang-format off */
#define __copy_user(to, from, size)					\
do {									\
	int __d0, __d1, __d2;						\
	__asm__ __volatile__(						\
		"	cmp  $7,%0\n"					\
		"	jbe  1f\n"					\
		"	movl %1,%0\n"					\
		"	negl %0\n"					\
		"	andl $7,%0\n"					\
		"	subl %0,%3\n"					\
		"4:	rep; movsb\n"					\
		"	movl %3,%0\n"					\
		"	shrl $2,%0\n"					\
		"	andl $3,%3\n"					\
		"	.align 2,0x90\n"				\
		"0:	rep; movsl\n"					\
		"	movl %3,%0\n"					\
		"1:	rep; movsb\n"					\
		"2:\n"							\
		_ASM_EXTABLE_TYPE_REG(4b, 2b, EX_TYPE_UCOPY_LEN1, %3)	\
		_ASM_EXTABLE_TYPE_REG(0b, 2b, EX_TYPE_UCOPY_LEN4, %3)	\
		_ASM_EXTABLE_UA(1b, 2b)					\
		: "=&c"(size), "=&D" (__d0), "=&S" (__d1), "=r"(__d2)	\
		: "3"(size), "0"(size), "1"(to), "2"(from)		\
		: "memory");						\
} while (0)
/* clang-format on */

unsigned long __copy_user_ll(void *to, const void *from, unsigned long n)
{
	__uaccess_begin_nospec();
	__copy_user(to, from, n);
	__uaccess_end();
	return n;
}

/* __copy_from_user_ll_nocache_nozero removed - never called */
