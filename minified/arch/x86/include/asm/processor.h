/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PROCESSOR_H
#define _ASM_X86_PROCESSOR_H

#include <asm/segment.h>
#include <asm/current.h>
#include <asm/cpufeatures.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>
#include <asm/msr.h>
#include <asm/desc_defs.h>
#include <asm/special_insns.h>

static __always_inline void cpu_relax(void)
{
	asm volatile("rep; nop" ::: "memory");
}

#include <linux/personality.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/irqflags.h>

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head_32.S, so think twice
 *  before touching them. [mj]
 */

struct cpuinfo_x86 {
	__u8 x86; /* CPU family */
	/* Maximum supported CPUID level, -1=no CPUID: */
	int cpuid_level;
	/*
	 * Align to size of unsigned long because the x86_capability array
	 * is passed to bitops which require the alignment. Use unnamed
	 * union to enforce the array is aligned to size of unsigned long.
	 */
	union {
		__u32 x86_capability[NCAPINTS + NBUGINTS];
	};
	char x86_vendor_id[16];
	int x86_cache_alignment; /* In bytes */
	u16 x86_clflush_size;
} __randomize_layout;

/*
 * capabilities of CPUs
 */
extern struct cpuinfo_x86 boot_cpu_data;

#define cache_line_size() (boot_cpu_data.x86_cache_alignment)

extern void early_cpu_init(void);
extern void identify_boot_cpu(void);

static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
				unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile("cpuid"
		     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
		     : "0"(*eax), "2"(*ecx)
		     : "memory");
}

static inline void load_cr3(pgd_t *pgdir)
{
	write_cr3(__sme_pa(pgdir));
}

/* This is the TSS defined by the hardware. */
struct x86_hw_tss {
	unsigned short back_link, __blh;
	unsigned long sp0;
	unsigned short ss0, __ss0h;
	unsigned long sp1;

	/*
	 * We don't use ring 1, so ss1 is a convenient scratch space in
	 * the same cacheline as sp0.  We use ss1 to cache the value in
	 * MSR_IA32_SYSENTER_CS.  When we context switch
	 * MSR_IA32_SYSENTER_CS, we first check if the new value being
	 * written matches ss1, and, if it's not, then we wrmsr the new
	 * value and update ss1.
	 *
	 * The only reason we context switch MSR_IA32_SYSENTER_CS is
	 * that we set it to zero in vm86 tasks to avoid corrupting the
	 * stack if we were to go through the sysenter path from vm86
	 * mode.
	 */
	unsigned short ss1; /* MSR_IA32_SYSENTER_CS */
	unsigned short __ss1h;
	unsigned long sp2;
	unsigned short ss2, __ss2h;
	unsigned long __cr3;
	unsigned long ip;
	unsigned long flags;
	unsigned long ax;
	unsigned long cx;
	unsigned long dx;
	unsigned long bx;
	unsigned long sp;
	unsigned long bp;
	unsigned long si;
	unsigned long di;
	unsigned short es, __esh;
	unsigned short cs, __csh;
	unsigned short ss, __ssh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
	unsigned short ldt, __ldth;
	unsigned short trace;
	unsigned short io_bitmap_base;
} __attribute__((packed));

#define __KERNEL_TSS_LIMIT \
	(offsetof(struct tss_struct, x86_tss) + sizeof(struct x86_hw_tss) - 1)

/* Base offset outside of TSS_LIMIT so unpriviledged IO causes #GP */
#define IO_BITMAP_OFFSET_INVALID (__KERNEL_TSS_LIMIT + 1)

struct tss_struct {
	/*
	 * The fixed hardware portion.  This must not cross a page boundary
	 * at risk of violating the SDM's advice and potentially triggering
	 * errata.
	 */
	struct x86_hw_tss x86_tss;
} __aligned(PAGE_SIZE);

DECLARE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw);

/* Per CPU interrupt stacks */
struct irq_stack {
	char stack[IRQ_STACK_SIZE];
} __aligned(IRQ_STACK_SIZE);

DECLARE_PER_CPU(unsigned long, cpu_current_top_of_stack);

struct thread_struct {
	unsigned long sp0;
	unsigned long sp;
	unsigned long gs;
};

static inline void native_load_sp0(unsigned long sp0)
{
	this_cpu_write(cpu_tss_rw.x86_tss.sp0, sp0);
}

#define __cpuid native_cpuid

static inline void load_sp0(unsigned long sp0)
{
	native_load_sp0(sp0);
}

static inline void cpuid(unsigned int op, unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = 0;
	__cpuid(eax, ebx, ecx, edx);
}

static inline void cpuid_count(unsigned int op, int count, unsigned int *eax,
			       unsigned int *ebx, unsigned int *ecx,
			       unsigned int *edx)
{
	*eax = op;
	*ecx = count;
	__cpuid(eax, ebx, ecx, edx);
}

extern void cpu_init(void);
extern void cpu_init_exception_handling(void);

#define TOP_OF_INIT_STACK                                  \
	((unsigned long)&init_stack + sizeof(init_stack) - \
	 TOP_OF_KERNEL_STACK_PADDING)

#define task_pt_regs(task)                                                  \
	({                                                                  \
		unsigned long __ptr = (unsigned long)task_stack_page(task); \
		__ptr += THREAD_SIZE - TOP_OF_KERNEL_STACK_PADDING;         \
		((struct pt_regs *)__ptr) - 1;                              \
	})

#define INIT_THREAD                       \
	{                                 \
		.sp0 = TOP_OF_INIT_STACK, \
	}

extern void start_thread(struct pt_regs *regs, unsigned long new_ip,
			 unsigned long new_sp);

#endif
