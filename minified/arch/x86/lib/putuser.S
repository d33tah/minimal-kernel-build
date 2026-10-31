/* SPDX-License-Identifier: GPL-2.0 */
/*
 * __put_user functions.
 *
 * (C) Copyright 2005 Linus Torvalds
 * (C) Copyright 2005 Andi Kleen
 * (C) Copyright 2008 Glauber Costa
 *
 * These functions have a non-standard call interface
 * to make them more efficient, especially as they
 * return an error value in addition to the "real"
 * return value.
 */
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/asm.h>

/*
 * __put_user_X
 *
 * Inputs:	%eax[:%edx] contains the data
 *		%ecx contains the address
 *
 * Outputs:	%ecx is error code (0 or -EFAULT)
 *
 * Clobbers:	%ebx needed for task pointer
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

#define LOAD_TASK_SIZE_MINUS_N(n) 	mov $(TASK_SIZE_MAX - (n)),%_ASM_BX

.text
SYM_FUNC_START(__put_user_4)
	LOAD_TASK_SIZE_MINUS_N(3)
	cmp %_ASM_BX,%_ASM_CX
	jae .Lbad_put_user
3:	movl %eax,(%_ASM_CX)
	xor %ecx,%ecx
	RET
SYM_FUNC_END(__put_user_4)

SYM_CODE_START_LOCAL(.Lbad_put_user_clac)
.Lbad_put_user:
	movl $-EFAULT,%ecx
	RET
SYM_CODE_END(.Lbad_put_user_clac)

	_ASM_EXTABLE_UA(3b, .Lbad_put_user_clac)
