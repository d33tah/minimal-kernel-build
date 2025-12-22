/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/export.h>

#include <asm/asm.h>

/*
 * unsigned int __sw_hweight32(unsigned int w)
 * %rdi: w
 */
SYM_FUNC_START(__sw_hweight32)
/* X86_32 only - no %edi copy needed */
	__ASM_SIZE(push,) %__ASM_REG(dx)
	movl %eax, %edx				# w -> t
	shrl %edx				# t >>= 1
	andl $0x55555555, %edx			# t &= 0x55555555
	subl %edx, %eax				# w -= t

	movl %eax, %edx				# w -> t
	shrl $2, %eax				# w_tmp >>= 2
	andl $0x33333333, %edx			# t	&= 0x33333333
	andl $0x33333333, %eax			# w_tmp &= 0x33333333
	addl %edx, %eax				# w = w_tmp + t

	movl %eax, %edx				# w -> t
	shrl $4, %edx				# t >>= 4
	addl %edx, %eax				# w_tmp += t
	andl  $0x0f0f0f0f, %eax			# w_tmp &= 0x0f0f0f0f
	imull $0x01010101, %eax, %eax		# w_tmp *= 0x01010101
	shrl $24, %eax				# w = w_tmp >> 24
	__ASM_SIZE(pop,) %__ASM_REG(dx)
	RET
SYM_FUNC_END(__sw_hweight32)
EXPORT_SYMBOL(__sw_hweight32)

SYM_FUNC_START(__sw_hweight64)
/* X86_32 only - call __sw_hweight32 twice */
	/* We're getting an u64 arg in (%eax,%edx): unsigned long hweight64(__u64 w) */
	pushl   %ecx

	call    __sw_hweight32
	movl    %eax, %ecx                      # stash away result
	movl    %edx, %eax                      # second part of input
	call    __sw_hweight32
	addl    %ecx, %eax                      # result

	popl    %ecx
	RET
SYM_FUNC_END(__sw_hweight64)
EXPORT_SYMBOL(__sw_hweight64)
