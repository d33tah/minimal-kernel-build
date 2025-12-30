/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/processor-flags.h>

.text

/*
 * Emulate 'cmpxchg8b (%esi)' on UP
 *
 * Inputs:
 * %esi : memory location to compare
 * %eax : low 32 bits of old value
 * %edx : high 32 bits of old value
 * %ebx : low 32 bits of new value
 * %ecx : high 32 bits of new value
 */
SYM_FUNC_START(cmpxchg8b_emu)

	pushfl
	cli

	cmpl	(%esi), %eax
	jne	.Lnot_same
	cmpl	4(%esi), %edx
	jne	.Lnot_same

	movl	%ebx, (%esi)
	movl	%ecx, 4(%esi)

	orl	$X86_EFLAGS_ZF, (%esp)

	popfl
	RET

.Lnot_same:
	movl	(%esi), %eax
	movl	4(%esi), %edx

	andl	$(~X86_EFLAGS_ZF), (%esp)

	popfl
	RET

SYM_FUNC_END(cmpxchg8b_emu)
