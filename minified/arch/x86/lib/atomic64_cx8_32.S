/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * atomic64_t for 586+
 *
 * Copyright © 2010  Luca Barbieri
 */

#include <linux/linkage.h>

.macro read64 reg
	movl %ebx, %eax
	movl %ecx, %edx
	cmpxchg8b (\reg)
.endm

SYM_FUNC_START(atomic64_read_cx8)
	read64 %ecx
	RET
SYM_FUNC_END(atomic64_read_cx8)

SYM_FUNC_START(atomic64_set_cx8)
1:
	cmpxchg8b (%esi)
	jne 1b

	RET
SYM_FUNC_END(atomic64_set_cx8)

SYM_FUNC_START(atomic64_inc_return_cx8)
	pushl %ebx

	read64 %esi
1:
	movl %eax, %ebx
	movl %edx, %ecx
	addl $1, %ebx
	adcl $0, %ecx
	cmpxchg8b (%esi)
	jne 1b

	movl %ebx, %eax
	movl %ecx, %edx
	popl %ebx
	RET
SYM_FUNC_END(atomic64_inc_return_cx8)
