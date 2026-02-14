/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * verify_cpu.S - Simplified CPU verification for 32-bit minimal kernel.
 * Only checks required CPU features (FPU, PSE, MSR, PAE, etc.) and SSE.
 * Longmode checks, AMD SSE forcing, Intel XD_DISABLE clearing removed.
 */

#include <asm/cpufeatures.h>

SYM_FUNC_START_LOCAL(verify_cpu)
	pushf
	push	$0
	popf

	pushfl				# check for cpuid support
	popl	%eax
	movl	%eax,%ebx
	xorl	$0x200000,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	cmpl	%eax,%ebx
	jz	.Lverify_cpu_fail	# no cpuid

	movl	$0x0,%eax		# check cpuid level
	cpuid
	cmpl	$0x1,%eax
	jb	.Lverify_cpu_fail

	movl	$0x1,%eax		# check required features
	cpuid
	andl	$REQUIRED_MASK0,%edx
	xorl	$REQUIRED_MASK0,%edx
	jnz	.Lverify_cpu_fail

	movl	$1,%eax			# check SSE
	cpuid
	andl	$SSE_MASK,%edx
	cmpl	$SSE_MASK,%edx
	jne	.Lverify_cpu_fail

	popf
	xorl	%eax,%eax
	RET
.Lverify_cpu_fail:
	popf
	movl	$1,%eax
	RET
SYM_FUNC_END(verify_cpu)
