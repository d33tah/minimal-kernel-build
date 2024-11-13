// SPDX-License-Identifier: GPL-2.0
/*
 * User address space access functions.
 * The non inlined parts of asm-i386/uaccess.h are here.
 *
 * Copyright 1997 Andi Kleen <ak@muc.de>
 * Copyright 1997 Linus Torvalds
 */
#include <linux/export.h>
#include <linux/uaccess.h>
#include <asm/asm.h>


static inline int __movsl_is_ok(unsigned long a1, unsigned long a2, unsigned long n)
{
	return 1;
}
#define movsl_is_ok(a1, a2, n) \
	__movsl_is_ok((unsigned long)(a1), (unsigned long)(a2), (n))

/*
 * Zero Userspace
 */

#define __do_clear_user(addr,size)					\
do {									\
	int __d0;							\
	might_fault();							\
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

/**
 * clear_user - Zero a block of memory in user space.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.
 *
 * Return: number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long
clear_user(void __user *to, unsigned long n)
{
	might_fault();
	if (access_ok(to, n))
		__do_clear_user(to, n);
	return n;
}
EXPORT_SYMBOL(clear_user);

/**
 * __clear_user - Zero a block of memory in user space, with less checking.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Return: number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long
__clear_user(void __user *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}
EXPORT_SYMBOL(__clear_user);


/*
 * Leave these declared but undefined.  They should not be any references to
 * them
 */
unsigned long __copy_user_intel(void __user *to, const void *from,
					unsigned long size);

/* Generic arbitrary sized copy.  */
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

unsigned long __copy_user_ll(void *to, const void *from, unsigned long n)
{
	__uaccess_begin_nospec();
	if (movsl_is_ok(to, from, n))
		__copy_user(to, from, n);
	else
		n = __copy_user_intel(to, from, n);
	__uaccess_end();
	return n;
}
EXPORT_SYMBOL(__copy_user_ll);

unsigned long __copy_from_user_ll_nocache_nozero(void *to, const void __user *from,
					unsigned long n)
{
	__uaccess_begin_nospec();
	__copy_user(to, from, n);
	__uaccess_end();
	return n;
}
EXPORT_SYMBOL(__copy_from_user_ll_nocache_nozero);
