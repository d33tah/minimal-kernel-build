/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Enhanced CPU detection and feature setting code by Mike Jagdis
 *  and Martin Mares, November 1997.
 */

.text
#include <linux/init.h>
#include <asm/segment.h>
#include <asm/pgtable_types.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/setup.h>
#include <asm/processor-flags.h>
#include <asm/nospec-branch.h>
#include <asm/pgtable_32.h>

/* Physical address */
#define pa(X) ((X) - __PAGE_OFFSET)

#define SIZEOF_PTREGS 17*4

/*
 * Worst-case size of the kernel mapping we need to make:
 * a relocatable kernel can live anywhere in lowmem, so we need to be able
 * to map all of lowmem.
 */
KERNEL_PAGES = LOWMEM_PAGES

INIT_MAP_SIZE = PAGE_TABLE_SIZE(KERNEL_PAGES) * PAGE_SIZE
RESERVE_BRK(pagetables, INIT_MAP_SIZE)

/*
 * 32-bit kernel entrypoint; only used by the boot CPU.  On entry,
 * %esi points to the real-mode code as a 32-bit pointer.
 * CS and DS must be 4 GB flat segments, but we don't depend on
 * any particular GDT layout, because we load our own as soon as we
 * can.
 */
__HEAD
SYM_CODE_START(startup_32)
	movl pa(initial_stack),%ecx
	
/*
 * Set segments to known values.
 */
	lgdt pa(boot_gdt_descr)
	movl $(__BOOT_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs
	movl %eax,%ss
	leal -__PAGE_OFFSET(%ecx),%esp

/*
 * Clear BSS first so that there are no surprises...
 */
	cld
	xorl %eax,%eax
	movl $pa(__bss_start),%edi
	movl $pa(__bss_stop),%ecx
	subl %edi,%ecx
	shrl $2,%ecx
	rep ; stosl
/*
 * Copy bootup parameters out of the way.
 * Note: %esi still has the pointer to the real-mode data.
 * With the kexec as boot loader, parameter segment might be loaded beyond
 * kernel image and might not even be addressable by early boot page tables.
 * (kexec on panic case). Hence copy out the parameters before initializing
 * page tables.
 */
	movl $pa(boot_params),%edi
	movl $(PARAM_SIZE/4),%ecx
	cld
	rep
	movsl

	/* Create early pagetables. */
	call  mk_early_pgtbl_32


	movl $(CR0_STATE & ~X86_CR0_PG),%eax
	movl %eax,%cr0

/*
 * We want to start out with EFLAGS unambiguously cleared. Some BIOSes leave
 * bits like NT set. This would confuse the debugger if this code is traced. So
 * initialize them properly now before switching to protected mode. That means
 * DF in particular (even though we have cleared it earlier after copying the
 * command line) because GCC expects it.
 */
	pushl $0
	popfl

/*
 * New page tables may be in 4Mbyte page mode and may be using the global pages.
 *
 * NOTE! If we are on a 486 we may have no cr4 at all! Specifically, cr4 exists
 * if and only if CPUID exists and has flags other than the FPU flag set.
 */
	movl $X86_EFLAGS_ID,%ecx
	pushl %ecx
	popfl				# set EFLAGS=ID
	pushfl
	popl %eax			# get EFLAGS
	testl $X86_EFLAGS_ID,%eax	# did EFLAGS.ID remained set?
	jz .Lenable_paging		# hw disallowed setting of ID bit
					# which means no CPUID and no CR4

	movl $1,%eax
	cpuid
	andl $~1,%edx			# Ignore CPUID.FPU
	jz .Lenable_paging		# No flags or only CPUID.FPU = no CR4

	movl pa(mmu_cr4_features),%eax
	movl %eax,%cr4

.Lenable_paging:

/*
 * Enable paging
 */
	movl $pa(initial_page_table), %eax
	movl %eax,%cr3		/* set the page table pointer.. */
	movl $CR0_STATE,%eax
	movl %eax,%cr0		/* ..and set paging (PG) bit */
	ljmp $__BOOT_CS,$1f	/* Clear prefetch and normalize %eip */
1:
	/* Shift the stack pointer to a virtual address */
	addl $__PAGE_OFFSET, %esp

	movl $0x50022,%ecx	# set AM, WP, NE and MP
	movl %cr0,%eax
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl %ecx,%eax
	movl %eax,%cr0

	lgdt early_gdt_descr
	ljmp $(__KERNEL_CS),$1f
1:	movl $(__KERNEL_DS),%eax	# reload all the segment registers
	movl %eax,%ss			# after changing gdt.

	movl $(__USER_DS),%eax		# DS/ES contains default USER segment
	movl %eax,%ds
	movl %eax,%es

	movl $(__KERNEL_PERCPU), %eax
	movl %eax,%fs			# set this cpu's percpu

	xorl %eax,%eax
	movl %eax,%gs			# clear possible garbage in %gs

	xorl %eax,%eax			# Clear LDT
	lldt %ax

	call i386_start_kernel
1:	jmp 1b
SYM_CODE_END(startup_32)


__INIT

SYM_FUNC_START(early_idt_handler_array)
	# 36(%esp) %eflags
	# 32(%esp) %cs
	# 28(%esp) %eip
	# 24(%rsp) error code
	i = 0
	.rept NUM_EXCEPTION_VECTORS
	.if ((EXCEPTION_ERRCODE_MASK >> i) & 1) == 0
	pushl $0		# Dummy error code, to make stack frame uniform
	.endif
	pushl $i		# 20(%esp) Vector number
	jmp early_idt_handler_common
	i = i + 1
	.fill early_idt_handler_array + i*EARLY_IDT_HANDLER_SIZE - ., 1, 0xcc
	.endr
SYM_FUNC_END(early_idt_handler_array)
	
SYM_CODE_START_LOCAL(early_idt_handler_common)
	/*
	 * The stack is the hardware frame, an error code or zero, and the
	 * vector number.
	 */
	cld

	incl %ss:early_recursion_flag

	/* The vector number is in pt_regs->gs */

	cld
	pushl	%fs		/* pt_regs->fs (__fsh varies by model) */
	pushl	%es		/* pt_regs->es (__esh varies by model) */
	pushl	%ds		/* pt_regs->ds (__dsh varies by model) */
	pushl	%eax		/* pt_regs->ax */
	pushl	%ebp		/* pt_regs->bp */
	pushl	%edi		/* pt_regs->di */
	pushl	%esi		/* pt_regs->si */
	pushl	%edx		/* pt_regs->dx */
	pushl	%ecx		/* pt_regs->cx */
	pushl	%ebx		/* pt_regs->bx */

	/* Fix up DS and ES */
	movl	$(__KERNEL_DS), %ecx
	movl	%ecx, %ds
	movl	%ecx, %es

	/* Load the vector number into EDX */
	movl	PT_GS(%esp), %edx

	/* Load GS into pt_regs->gs (and maybe clobber __gsh) */
	movw	%gs, PT_GS(%esp)

	movl	%esp, %eax	/* args are pt_regs (EAX), trapnr (EDX) */
	call	early_fixup_exception

	popl	%ebx		/* pt_regs->bx */
	popl	%ecx		/* pt_regs->cx */
	popl	%edx		/* pt_regs->dx */
	popl	%esi		/* pt_regs->si */
	popl	%edi		/* pt_regs->di */
	popl	%ebp		/* pt_regs->bp */
	popl	%eax		/* pt_regs->ax */
	popl	%ds		/* pt_regs->ds (always ignores __dsh) */
	popl	%es		/* pt_regs->es (always ignores __esh) */
	popl	%fs		/* pt_regs->fs (always ignores __fsh) */
	popl	%gs		/* pt_regs->gs (always ignores __gsh) */
	decl	%ss:early_recursion_flag
	addl	$4, %esp	/* pop pt_regs->orig_ax */
	iret
SYM_CODE_END(early_idt_handler_common)

/* This is the default interrupt "handler" :-) */
SYM_FUNC_START(early_ignore_irq)
	cld
	iret

hlt_loop:
	hlt
	jmp hlt_loop
SYM_FUNC_END(early_ignore_irq)

__INITDATA
	.align 4
SYM_DATA(early_recursion_flag, .long 0)

#define	PGD_ALIGN	(PAGE_SIZE)
#define PTI_USER_PGD_FILL	0
/*
 * BSS section
 */
__PAGE_ALIGNED_BSS
	.align PGD_ALIGN
.globl initial_page_table
initial_page_table:
	.fill 1024,4,0
	.align PGD_ALIGN
.globl swapper_pg_dir
	.align PGD_ALIGN
swapper_pg_dir:
	.fill 1024,4,0
	.fill PTI_USER_PGD_FILL,4,0
.globl empty_zero_page
empty_zero_page:
	.fill 4096,1,0

/*
 * This starts the data section.
 */

.data
.balign 4
/*
 * The SIZEOF_PTREGS gap is a convention which helps the in-kernel unwinder
 * reliably detect the end of the stack.
 */
SYM_DATA(initial_stack,
		.long init_thread_union + THREAD_SIZE -
		SIZEOF_PTREGS - TOP_OF_KERNEL_STACK_PADDING)

/*
 * The IDT and GDT 'descriptors' are a strange 48-bit object
 * only used by the lidt and lgdt instructions. They are not
 * like usual segment descriptors - they consist of a 16-bit
 * segment size, and 32-bit linear address value:
 */

	.data
	ALIGN
# early boot GDT descriptor (must use 1:1 address mapping)
	.word 0				# 32 bit align gdt_desc.address
SYM_DATA_START_LOCAL(boot_gdt_descr)
	.word __BOOT_DS+7
	.long boot_gdt - __PAGE_OFFSET
SYM_DATA_END(boot_gdt_descr)

# boot GDT descriptor (later on used by CPU#0):
	.word 0				# 32 bit align gdt_desc.address
SYM_DATA_START(early_gdt_descr)
	.word GDT_ENTRIES*8-1
	.long gdt_page			/* Overwritten for secondary CPUs */
SYM_DATA_END(early_gdt_descr)

/*
 * The boot_gdt must mirror the equivalent in setup.S and is
 * used only for booting.
 */
	.align L1_CACHE_BYTES
SYM_DATA_START(boot_gdt)
	.fill GDT_ENTRY_BOOT_CS,8,0
	.quad 0x00cf9a000000ffff	/* kernel 4GB code at 0x00000000 */
	.quad 0x00cf92000000ffff	/* kernel 4GB data at 0x00000000 */
SYM_DATA_END(boot_gdt)
