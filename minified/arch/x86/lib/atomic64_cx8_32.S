/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * atomic64_t for 586+
 *
 * Copyright © 2010  Luca Barbieri
 */

#include <linux/linkage.h>
#include <asm/alternative.h>

.macro read64 reg
	movl %ebx, %eax
	movl %ecx, %edx
/* we need LOCK_PREFIX since otherwise cmpxchg8b always does the write */
	LOCK_PREFIX
	cmpxchg8b (\reg)
.endm

SYM_FUNC_START(atomic64_read_cx8)
	read64 %ecx
	RET
SYM_FUNC_END(atomic64_read_cx8)

SYM_FUNC_START(atomic64_set_cx8)
1:
/* we don't need LOCK_PREFIX since aligned 64-bit writes
 * are atomic on 586 and newer */
	cmpxchg8b (%esi)
	jne 1b

	RET
SYM_FUNC_END(atomic64_set_cx8)

.macro addsub_return func ins insc
SYM_FUNC_START(atomic64_\func\()_return_cx8)
	pushl %ebp
	pushl %ebx
	pushl %esi
	pushl %edi

	movl %eax, %esi
	movl %edx, %edi
	movl %ecx, %ebp

	read64 %ecx
1:
	movl %eax, %ebx
	movl %edx, %ecx
	\ins\()l %esi, %ebx
	\insc\()l %edi, %ecx
	LOCK_PREFIX
	cmpxchg8b (%ebp)
	jne 1b

10:
	movl %ebx, %eax
	movl %ecx, %edx
	popl %edi
	popl %esi
	popl %ebx
	popl %ebp
	RET
SYM_FUNC_END(atomic64_\func\()_return_cx8)
.endm

addsub_return add add adc

.macro incdec_return func ins insc
SYM_FUNC_START(atomic64_\func\()_return_cx8)
	pushl %ebx

	read64 %esi
1:
	movl %eax, %ebx
	movl %edx, %ecx
	\ins\()l $1, %ebx
	\insc\()l $0, %ecx
	LOCK_PREFIX
	cmpxchg8b (%esi)
	jne 1b

10:
	movl %ebx, %eax
	movl %ecx, %edx
	popl %ebx
	RET
SYM_FUNC_END(atomic64_\func\()_return_cx8)
.endm

incdec_return inc add adc

/* Removed unused assembly functions: xchg, sub_return, dec_return,
 * dec_if_positive, add_unless, inc_not_zero (~100 LOC) */
