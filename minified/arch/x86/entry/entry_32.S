/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 1991,1992  Linus Torvalds
 *
 * entry_32.S contains the system-call and low-level fault and trap handling routines.
 *
 * Stack layout while running C code:
 *	 0(%esp) - %ebx
 *	 4(%esp) - %ecx
 *	 8(%esp) - %edx
 *	 C(%esp) - %esi
 *	10(%esp) - %edi
 *	14(%esp) - %ebp
 *	18(%esp) - %eax
 *	1C(%esp) - %ds
 *	20(%esp) - %es
 *	24(%esp) - %fs
 *	28(%esp) - unused -- was %gs on old stackprotector kernels
 *	2C(%esp) - orig_eax
 *	30(%esp) - %eip
 *	34(%esp) - %cs
 *	38(%esp) - %eflags
 *	3C(%esp) - %oldesp
 *	40(%esp) - %oldss
 */

#include <linux/linkage.h>
#include <linux/err.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/irq_vectors.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <asm/nospec-branch.h>
#include <linux/jump_label.h>
#include <asm/unwind_hints.h>
#include <asm/page_types.h>
#include <generated/asm-offsets.h>
#include <asm/msr.h>
# undef		UNWIND_HINT_IRET_REGS
# define	UNWIND_HINT_IRET_REGS

	.section .entry.text, "ax"

/* PTI disabled - CR3 switching macros are no-ops */
.macro SWITCH_TO_USER_CR3 scratch_reg:req
.endm

.macro SWITCH_TO_KERNEL_CR3 scratch_reg:req
.endm

#define CS_FROM_ENTRY_STACK	(1 << 31)
#define CS_FROM_KERNEL		(1 << 29)

.macro FIXUP_FRAME
	/* Clear high bits of CS dword used for CS_FROM_* markers */
	andl	$0x0000ffff, 4*4(%esp)

	testl	$USER_SEGMENT_RPL_MASK, 4*4(%esp)
	jnz	.Lfrom_usermode_no_fixup_\@

	orl	$CS_FROM_KERNEL, 4*4(%esp)

	/*
	 * Kernel-mode exception: build a 5-entry IRET frame so that
	 * struct pt_regs is complete and regs->sp is correct.
	 */
	pushl	%ss
	pushl	%esp
	addl	$7*4, (%esp)
	pushl	7*4(%esp)	# flags
	pushl	7*4(%esp)	# cs
	pushl	7*4(%esp)	# ip
	pushl	7*4(%esp)	# orig_eax
	pushl	7*4(%esp)	# gs / function
	pushl	7*4(%esp)	# fs
.Lfrom_usermode_no_fixup_\@:
.endm

.macro IRET_FRAME
	testl $CS_FROM_KERNEL, 1*4(%esp)
	jz .Lfinished_frame_\@

	/* Reconstruct 3-entry IRET frame at regs->sp without lowering %esp */
	pushl	%eax
	pushl	%ecx
	movl	5*4(%esp), %eax		# (modified) regs->sp

	movl	4*4(%esp), %ecx		# flags
	movl	%ecx, %ss:-1*4(%eax)

	movl	3*4(%esp), %ecx		# cs
	andl	$0x0000ffff, %ecx
	movl	%ecx, %ss:-2*4(%eax)

	movl	2*4(%esp), %ecx		# ip
	movl	%ecx, %ss:-3*4(%eax)

	movl	1*4(%esp), %ecx		# eax
	movl	%ecx, %ss:-4*4(%eax)

	popl	%ecx
	lea	-4*4(%eax), %esp
	popl	%eax
.Lfinished_frame_\@:
.endm

.macro SAVE_ALL pt_regs_ax=%eax switch_stacks=0 skip_gs=0
	cld
.if \skip_gs == 0
	pushl	$0
.endif
	pushl	%fs

	pushl	%eax
	movl	$(__KERNEL_PERCPU), %eax
	movl	%eax, %fs
	popl	%eax

	FIXUP_FRAME
	pushl	%es
	pushl	%ds
	pushl	\pt_regs_ax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	movl	$(__USER_DS), %edx
	movl	%edx, %ds
	movl	%edx, %es
.if \switch_stacks > 0
	SWITCH_TO_KERNEL_STACK
.endif
.endm

.macro SAVE_ALL_NMI cr3_reg:req
	SAVE_ALL
.endm

.macro RESTORE_INT_REGS
	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp
	popl	%eax
.endm

.macro RESTORE_REGS pop=0
	RESTORE_INT_REGS
1:	popl	%ds
2:	popl	%es
3:	popl	%fs
4:	addl	$(4 + \pop), %esp	/* pop the unused "gs" slot */
	IRET_FRAME

	_ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_POP_ZERO|EX_REG_DS)
	_ASM_EXTABLE_TYPE(2b, 3b, EX_TYPE_POP_ZERO|EX_REG_ES)
	_ASM_EXTABLE_TYPE(3b, 4b, EX_TYPE_POP_ZERO|EX_REG_FS)
.endm

.macro RESTORE_ALL_NMI cr3_reg:req pop=0
	RESTORE_REGS pop=\pop
.endm


/*
 * SWITCH_TO_KERNEL_STACK: called with pt_regs on stack and kernel
 * segments loaded. Copies entry-stack frame to task-stack if needed.
 */
.macro SWITCH_TO_KERNEL_STACK

	SWITCH_TO_KERNEL_CR3 scratch_reg=%eax

	/* Are we on the entry stack? Bail out if not! */
	movl	PER_CPU_VAR(cpu_entry_area), %ecx
	addl	$CPU_ENTRY_AREA_entry_stack + SIZEOF_entry_stack, %ecx
	subl	%esp, %ecx
	cmpl	$SIZEOF_entry_stack, %ecx
	jae	.Lend_\@

	movl	%esp, %esi
	movl	%esi, %edi

	/* Move %edi to the top of the entry stack */
	andl	$(MASK_entry_stack), %edi
	addl	$(SIZEOF_entry_stack), %edi

	/* Load top of task-stack into %edi */
	movl	TSS_entry2task_stack(%edi), %edi

	/* Special case - entry from kernel mode via entry stack */
	movl	PT_CS(%esp), %ecx
	andl	$SEGMENT_RPL_MASK, %ecx
	cmpl	$USER_RPL, %ecx
	jb	.Lentry_from_kernel_\@

	movl	$PTREGS_SIZE, %ecx

.Lcopy_pt_regs_\@:
	subl	%ecx, %edi
	movl	%edi, %esp

	shrl	$2, %ecx
	cld
	rep movsl

	jmp .Lend_\@

.Lentry_from_kernel_\@:
	/* Calculate number of bytes on the entry stack in %ecx */
	movl	%esi, %ecx
	andl	$(MASK_entry_stack), %ecx
	addl	$(SIZEOF_entry_stack), %ecx
	sub	%esi, %ecx

	/* Mark stackframe as coming from entry stack */
	orl	$CS_FROM_ENTRY_STACK, PT_CS(%esp)

.Lend_\@:
.endm

/*
 * SWITCH_TO_ENTRY_STACK: copy task-stack pt_regs to entry-stack for iret.
 */
.macro SWITCH_TO_ENTRY_STACK
	movl	$PTREGS_SIZE, %ecx
	movl	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %edi
	subl	%ecx, %edi
	movl	%esp, %esi
	movl	%edi, %ebx

	shrl	$2, %ecx
	cld
	rep movsl

	movl	%ebx, %esp

.Lend_\@:
.endm

/*
 * PARANOID_EXIT_TO_KERNEL_MODE: on return-to-kernel, switch back to
 * entry-stack if we came from there.
 */
.macro PARANOID_EXIT_TO_KERNEL_MODE
	testl	$CS_FROM_ENTRY_STACK, PT_CS(%esp)
	jz	.Lend_\@

	andl	$(~CS_FROM_ENTRY_STACK), PT_CS(%esp)

	movl	%esp, %esi
	movl	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %edi

	movl	PER_CPU_VAR(cpu_tss_rw + TSS_sp1), %ecx
	subl	%esi, %ecx

	subl	%ecx, %edi
	movl	%edi, %ebx

	shrl	$2, %ecx
	cld
	rep movsl

	movl	%ebx, %esp

.Lend_\@:
.endm

/**
 * idtentry - Macro to generate entry stubs for simple IDT entries
 */
.macro idtentry vector asmsym cfunc has_error_code:req
SYM_CODE_START(\asmsym)
	ASM_CLAC
	cld

	.if \has_error_code == 0
		pushl	$0		/* Clear the error code */
	.endif

	pushl	$\cfunc
	jmp	handle_exception
SYM_CODE_END(\asmsym)
.endm

.macro idtentry_irq vector cfunc
	.p2align CONFIG_X86_L1_CACHE_SHIFT
SYM_CODE_START_LOCAL(asm_\cfunc)
	ASM_CLAC
	SAVE_ALL switch_stacks=1

	movl	%esp, %eax
	movl	PT_ORIG_EAX(%esp), %edx		/* get the vector from stack */
	movl	$-1, PT_ORIG_EAX(%esp)		/* no syscall to restart */
	call	\cfunc
	jmp	handle_exception_return
SYM_CODE_END(asm_\cfunc)
.endm

	.align 16
	.globl __irqentry_text_start
__irqentry_text_start:

#include <asm/idtentry.h>

	.align 16
	.globl __irqentry_text_end
__irqentry_text_end:

/*
 * __switch_to_asm: %eax=prev, %edx=next
 */
.pushsection .text, "ax"
SYM_CODE_START(__switch_to_asm)
	pushl	%ebp
	pushl	%ebx
	pushl	%edi
	pushl	%esi
	pushfl

	/* switch stack */
	movl	%esp, TASK_threadsp(%eax)
	movl	TASK_threadsp(%edx), %esp

	FILL_RETURN_BUFFER %ebx, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_CTXSW

	popfl
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp

	jmp	__switch_to
SYM_CODE_END(__switch_to_asm)
.popsection

.pushsection .text, "ax"
SYM_FUNC_START(schedule_tail_wrapper)
	pushl	%eax
	call	schedule_tail
	popl	%eax
	RET
SYM_FUNC_END(schedule_tail_wrapper)
.popsection

/*
 * ret_from_fork: entered by newly forked processes.
 * eax: prev task, ebx: kernel thread func (NULL for user), edi: arg
 */
.pushsection .text, "ax"
SYM_CODE_START(ret_from_fork)
	call	schedule_tail_wrapper

	testl	%ebx, %ebx
	jnz	1f		/* kernel threads are uncommon */

2:
	movl    %esp, %eax
	call    syscall_exit_to_user_mode
	jmp     .Lsyscall_32_done

	/* kernel thread */
1:	movl	%edi, %eax
	CALL_NOSPEC ebx
	movl	$0, PT_EAX(%esp)
	jmp	2b
SYM_CODE_END(ret_from_fork)
.popsection

/* 32-bit legacy system call entry via INT $0x80 */
SYM_FUNC_START(entry_INT80_32)
	ASM_CLAC
	pushl	%eax			/* pt_regs->orig_ax */

	SAVE_ALL pt_regs_ax=$-ENOSYS switch_stacks=1

	movl	%esp, %eax
	call	do_int80_syscall_32
.Lsyscall_32_done:

restore_all_switch_stack:
	SWITCH_TO_ENTRY_STACK
	SWITCH_TO_USER_CR3 scratch_reg=%eax

	RESTORE_REGS pop=4			# skip orig_eax/error_code
.Lirq_return:
	iret

.Lasm_iret_error:
	pushl	$0				# no error code
	pushl	$iret_error
	jmp	handle_exception

	_ASM_EXTABLE(.Lirq_return, .Lasm_iret_error)
SYM_FUNC_END(entry_INT80_32)


SYM_CODE_START_LOCAL_NOALIGN(handle_exception)
	SAVE_ALL switch_stacks=1 skip_gs=1

	movl	PT_GS(%esp), %edi		# get the function address
	movl	PT_ORIG_EAX(%esp), %edx		# get the error code
	movl	$-1, PT_ORIG_EAX(%esp)		# no syscall to restart

	movl	%esp, %eax			# pt_regs pointer
	CALL_NOSPEC edi

handle_exception_return:
	movl	PT_CS(%esp), %eax
	andl	$SEGMENT_RPL_MASK, %eax
	cmpl	$USER_RPL, %eax			# returning to userspace?
	jnb	ret_to_user

	PARANOID_EXIT_TO_KERNEL_MODE

	RESTORE_REGS 4
	jmp	.Lirq_return

ret_to_user:
	movl	%esp, %eax
	jmp	restore_all_switch_stack
SYM_CODE_END(handle_exception)

SYM_CODE_START(asm_exc_nmi)
	ASM_CLAC
	pushl	%eax				# pt_regs->orig_ax
	SAVE_ALL_NMI cr3_reg=%edi
	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer
	call	exc_nmi
	RESTORE_ALL_NMI cr3_reg=%edi pop=4
	jmp	.Lirq_return
SYM_CODE_END(asm_exc_nmi)

.pushsection .text, "ax"
SYM_CODE_START(rewind_stack_and_make_dead)
	xorl	%ebp, %ebp
	movl	PER_CPU_VAR(cpu_current_top_of_stack), %esi
	leal	-TOP_OF_KERNEL_STACK_PADDING-PTREGS_SIZE(%esi), %esp
	call	make_task_dead
1:	jmp 1b
SYM_CODE_END(rewind_stack_and_make_dead)
.popsection
