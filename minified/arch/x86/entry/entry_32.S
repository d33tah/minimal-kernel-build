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
 *
 * Entries from user mode arrive on the task stack: TSS.sp0 is the top of
 * the current task's stack (load_sp0() in __switch_to and cpu_init), so
 * there is no entry trampoline stack to copy pt_regs from or back to.
 * There is no page-table isolation (no CR3 switching), no SYSENTER entry
 * (user space enters with int $0x80) and no ESPFIX.
 */

#include <linux/err.h>
#include <asm/segment.h>
#include <asm/irq_vectors.h>
#include <asm/smap.h>
#include <asm/frame.h>

#include "calling.h"

	.section .entry.text, "ax"

#define CS_FROM_KERNEL		(1 << 29)

/*
 * Exceptions from kernel mode do not push ss/esp: build a full pt_regs
 * frame so that regs->sp/ss are valid (marked with CS_FROM_KERNEL).
 */
.macro FIXUP_FRAME
	andl	$0x0000ffff, 4*4(%esp)

	testl	$USER_SEGMENT_RPL_MASK, 4*4(%esp)
	jnz	.Lfrom_usermode_no_fixup_\@

	orl	$CS_FROM_KERNEL, 4*4(%esp)

	pushl	%ss		# ss
	pushl	%esp		# sp (points at ss)
	addl	$7*4, (%esp)	# point sp back at the previous context
	pushl	7*4(%esp)	# flags
	pushl	7*4(%esp)	# cs
	pushl	7*4(%esp)	# ip
	pushl	7*4(%esp)	# orig_eax
	pushl	7*4(%esp)	# gs / function
	pushl	7*4(%esp)	# fs
.Lfrom_usermode_no_fixup_\@:
.endm

/* Undo FIXUP_FRAME: rebuild the 3-word iret frame below regs->sp. */
.macro IRET_FRAME
	testl $CS_FROM_KERNEL, 1*4(%esp)
	jz .Lfinished_frame_\@

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

.macro SAVE_ALL pt_regs_ax=%eax skip_gs=0
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

	/*
	 * There is no _ASM_EXTABLE_TYPE_REG() for ASM, however since this is
	 * ASM the registers are known and we can trivially hard-code them.
	 */
	_ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_POP_ZERO|EX_REG_DS)
	_ASM_EXTABLE_TYPE(2b, 3b, EX_TYPE_POP_ZERO|EX_REG_ES)
	_ASM_EXTABLE_TYPE(3b, 4b, EX_TYPE_POP_ZERO|EX_REG_FS)
.endm

/**
 * idtentry - Macro to generate entry stubs for simple IDT entries
 * @vector:		Vector number
 * @asmsym:		ASM symbol for the entry point
 * @cfunc:		C function to be called
 * @has_error_code:	Hardware pushed error code on stack
 */
.macro idtentry vector asmsym cfunc has_error_code:req
SYM_CODE_START(\asmsym)
	ASM_CLAC
	cld

	.if \has_error_code == 0
		pushl	$0		/* Clear the error code */
	.endif

	/* Push the C-function address into the GS slot */
	pushl	$\cfunc
	/* Invoke the common exception entry */
	jmp	handle_exception
SYM_CODE_END(\asmsym)
.endm

.macro idtentry_irq vector cfunc
	.p2align CONFIG_X86_L1_CACHE_SHIFT
SYM_CODE_START_LOCAL(asm_\cfunc)
	ASM_CLAC
	SAVE_ALL
	ENCODE_FRAME_POINTER
	movl	%esp, %eax
	movl	PT_ORIG_EAX(%esp), %edx		/* get the vector from stack */
	movl	$-1, PT_ORIG_EAX(%esp)		/* no syscall to restart */
	call	\cfunc
	jmp	handle_exception_return
SYM_CODE_END(asm_\cfunc)
.endm

/*
 * Include the defines which emit the idt entries which are shared
 * shared between 32 and 64 bit and emit the __irqentry_text_* markers
 * so the stacktrace boundary checks work.
 */
	.align 16
	.globl __irqentry_text_start
__irqentry_text_start:

#include <asm/idtentry.h>

	.align 16
	.globl __irqentry_text_end
__irqentry_text_end:

/*
 * %eax: prev task
 * %edx: next task
 */
.pushsection .text, "ax"
SYM_CODE_START(__switch_to_asm)
	/*
	 * Save callee-saved registers
	 * This must match the order in struct inactive_task_frame
	 */
	pushl	%ebp
	pushl	%ebx
	pushl	%edi
	pushl	%esi
	/*
	 * Flags are saved to prevent AC leakage. This could go
	 * away if objtool would have 32bit support to verify
	 * the STAC/CLAC correctness.
	 */
	pushfl

	/* switch stack */
	movl	%esp, TASK_threadsp(%eax)
	movl	TASK_threadsp(%edx), %esp

	/* Restore flags or the incoming task to restore AC state. */
	popfl
	/* restore callee-saved registers */
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp

	jmp	__switch_to
SYM_CODE_END(__switch_to_asm)
.popsection

/*
 * The unwinder expects the last frame on the stack to always be at the same
 * offset from the end of the page, which allows it to validate the stack.
 * Calling schedule_tail() directly would break that convention because its an
 * asmlinkage function so its argument has to be pushed on the stack.  This
 * wrapper creates a proper "end of stack" frame header before the call.
 */
.pushsection .text, "ax"
SYM_FUNC_START(schedule_tail_wrapper)
	FRAME_BEGIN

	pushl	%eax
	call	schedule_tail
	popl	%eax

	FRAME_END
	RET
SYM_FUNC_END(schedule_tail_wrapper)
.popsection

/*
 * A newly forked process directly context switches into this address.
 *
 * eax: prev task we switched from
 * ebx: kernel thread func (NULL for user thread)
 * edi: kernel thread arg
 */
.pushsection .text, "ax"
SYM_CODE_START(ret_from_fork)
	call	schedule_tail_wrapper

	testl	%ebx, %ebx
	jnz	1f		/* kernel threads are uncommon */

2:
	/* When we fork, we trace the syscall return in the child, too. */
	movl    %esp, %eax
	call    syscall_exit_to_user_mode
	jmp     .Lsyscall_32_done

	/* kernel thread */
1:	movl	%edi, %eax
	CALL_NOSPEC ebx
	/*
	 * A kernel thread is allowed to return here after successfully
	 * calling kernel_execve().  Exit to userspace to complete the execve()
	 * syscall.
	 */
	movl	$0, PT_EAX(%esp)
	jmp	2b
SYM_CODE_END(ret_from_fork)
.popsection

/*
 * 32-bit legacy system call entry: int $0x80.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  arg6
 */
SYM_FUNC_START(entry_INT80_32)
	ASM_CLAC
	pushl	%eax			/* pt_regs->orig_ax */

	SAVE_ALL pt_regs_ax=$-ENOSYS	/* save rest */

	movl	%esp, %eax
	call	do_int80_syscall_32
.Lsyscall_32_done:
	RESTORE_REGS pop=4			# skip orig_eax/error_code
.Lirq_return:
	/*
	 * ARCH_HAS_MEMBARRIER_SYNC_CORE rely on IRET core serialization
	 * when returning from IPI handler and when returning from
	 * scheduler to user-space.
	 */
	iret

.Lasm_iret_error:
	pushl	$0				# no error code
	pushl	$iret_error
	jmp	handle_exception

	_ASM_EXTABLE(.Lirq_return, .Lasm_iret_error)
SYM_FUNC_END(entry_INT80_32)

SYM_CODE_START_LOCAL_NOALIGN(handle_exception)
	/* the function address is in %gs's slot on the stack */
	SAVE_ALL skip_gs=1
	ENCODE_FRAME_POINTER

	movl	PT_GS(%esp), %edi		# get the function address

	/* fixup orig %eax */
	movl	PT_ORIG_EAX(%esp), %edx		# get the error code
	movl	$-1, PT_ORIG_EAX(%esp)		# no syscall to restart

	movl	%esp, %eax			# pt_regs pointer
	CALL_NOSPEC edi

handle_exception_return:
	RESTORE_REGS 4
	jmp	.Lirq_return
SYM_CODE_END(handle_exception)

SYM_CODE_START(asm_exc_double_fault)
1:
	/*
	 * This is a task gate handler, not an interrupt gate handler.
	 * The error code is on the stack, but the stack is otherwise
	 * empty.  Interrupts are off.  Our state is sane with the following
	 * exceptions:
	 *
	 *  - CR0.TS is set.  "TS" literally means "task switched".
	 *  - EFLAGS.NT is set because we're a "nested task".
	 *  - The doublefault TSS has back_link set and has been marked busy.
	 *  - TR points to the doublefault TSS and the normal TSS is busy.
	 *  - CR3 is the normal kernel PGD.  This would be delightful, except
	 *    that the CPU didn't bother to save the old CR3 anywhere.  This
	 *    would make it very awkward to return back to the context we came
	 *    from.
	 *
	 * The rest of EFLAGS is sanitized for us, so we don't need to
	 * worry about AC or DF.
	 *
	 * Don't even bother popping the error code.  It's always zero,
	 * and ignoring it makes us a bit more robust against buggy
	 * hypervisors.
	 */

	clts				/* clear CR0.TS */
	pushl	$X86_EFLAGS_FIXED
	popfl				/* clear EFLAGS.NT */

	call	doublefault_shim

	/* We don't support returning, so we have no IRET here. */
1:
	hlt
	jmp 1b
SYM_CODE_END(asm_exc_double_fault)

SYM_CODE_START(asm_exc_nmi)
	ASM_CLAC

	pushl	%eax				# pt_regs->orig_ax
	SAVE_ALL
	ENCODE_FRAME_POINTER

	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer
	call	exc_nmi

	RESTORE_REGS 4
	jmp	.Lirq_return
SYM_CODE_END(asm_exc_nmi)
