
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
/* Removed: #include <asm/vmxfeatures.h> - not needed for minimal kernel */

static __always_inline void cpu_relax(void)
{
	asm volatile("rep; nop" ::: "memory");
}

#include <linux/personality.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/irqflags.h>

/* NET_IP_ALIGN removed - unused */

#define ARCH_MIN_MMSTRUCT_ALIGN 0

/* TLB arrays removed - never used in minimal kernel */

struct cpuinfo_x86 {
	__u8 x86;
	union {
		__u32 x86_capability[NCAPINTS + NBUGINTS];
	};
	int x86_cache_alignment;
} __randomize_layout;

extern struct cpuinfo_x86 boot_cpu_data;

/* cpuinfo_op declaration removed - no implementation */

#define cache_line_size() (boot_cpu_data.x86_cache_alignment)

extern void early_cpu_init(void);
extern void identify_boot_cpu(void);
/* identify_secondary_cpu, print_cpu_info, print_cpu_msr removed - never called */

static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
				unsigned int *ecx, unsigned int *edx)
{
	asm volatile("cpuid"
		     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
		     : "0"(*eax), "2"(*ecx)
		     : "memory");
}

/* native_cpuid_reg + native_cpuid_{eax,ebx,ecx,edx} removed - unused */

/* native_read_cr3_pa removed - unused */

static inline void load_cr3(pgd_t *pgdir)
{
	write_cr3(__sme_pa(pgdir));
}

struct x86_hw_tss {
	unsigned short back_link, __blh;
	unsigned long sp0;
	unsigned short ss0, __ss0h;
	unsigned long sp1;
	unsigned short ss1;
	unsigned short __ss1h;
	unsigned long sp2;
	unsigned short ss2, __ss2h;
	unsigned long __cr3, ip, flags, ax, cx, dx, bx, sp, bp, si, di;
	unsigned short es, __esh;
	unsigned short cs, __csh;
	unsigned short ss, __ssh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
	unsigned short ldt, __ldth;
	unsigned short trace, io_bitmap_base;
} __attribute__((packed));

#define __KERNEL_TSS_LIMIT \
	(offsetof(struct tss_struct, x86_tss) + sizeof(struct x86_hw_tss) - 1)

#define IO_BITMAP_OFFSET_INVALID (__KERNEL_TSS_LIMIT + 1)

struct tss_struct {
	struct x86_hw_tss x86_tss;
} __aligned(PAGE_SIZE);

DECLARE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw);

struct irq_stack {
	char stack[IRQ_STACK_SIZE];
} __aligned(IRQ_STACK_SIZE);

DECLARE_PER_CPU(unsigned long, cpu_current_top_of_stack);

struct thread_struct {
	unsigned long sp0, sp;
	unsigned long gs;
};

static inline void native_load_sp0(unsigned long sp0)
{
	this_cpu_write(cpu_tss_rw.x86_tss.sp0, sp0);
}

/* current_top_of_stack removed - unused */

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

/* amd_e400_c1e_apic_setup declaration removed - implementation was removed */

extern struct desc_ptr early_gdt_descr;

extern void cpu_init(void);
/* cpu_init_secondary declaration removed - no implementation */
extern void cpu_init_exception_handling(void);
/* set_task_blockstep, bootloader_type, bootloader_version removed - unused */

/* ARCH_HAS_PREFETCHW, ARCH_HAS_SPINLOCK_PREFETCH, ARCH_HAS_PREFETCH removed - unused */

static inline void prefetch(const void *x)
{
	asm volatile("" : : "m"(*(const char *)x));
}

static __always_inline void prefetchw(const void *x)
{
	asm volatile("" : : "m"(*(const char *)x));
}

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
