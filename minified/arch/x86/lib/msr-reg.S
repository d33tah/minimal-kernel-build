/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/msr.h>

/* X86_32 only */
/*
 * int {rdmsr,wrmsr}_safe_regs(u32 gprs[8]);
 *
 * reg layout: u32 gprs[eax, ecx, edx, ebx, esp, ebp, esi, edi]
 *
 */
.macro op_safe_regs op
SYM_FUNC_START(\op\()_safe_regs)
	pushl %ebx
	pushl %ebp
	pushl %esi
	pushl %edi
	pushl $0              /* Return value */
	pushl %eax
	movl    4(%eax), %ecx
	movl    8(%eax), %edx
	movl    12(%eax), %ebx
	movl    20(%eax), %ebp
	movl    24(%eax), %esi
	movl    28(%eax), %edi
	movl    (%eax), %eax
1:	\op
2:	pushl %eax
	movl    4(%esp), %eax
	popl (%eax)
	addl    $4, %esp
	movl    %ecx, 4(%eax)
	movl    %edx, 8(%eax)
	movl    %ebx, 12(%eax)
	movl    %ebp, 20(%eax)
	movl    %esi, 24(%eax)
	movl    %edi, 28(%eax)
	popl %eax
	popl %edi
	popl %esi
	popl %ebp
	popl %ebx
	RET
3:
	movl    $-EIO, 4(%esp)
	jmp     2b

	_ASM_EXTABLE(1b, 3b)
SYM_FUNC_END(\op\()_safe_regs)
.endm

op_safe_regs rdmsr
op_safe_regs wrmsr

