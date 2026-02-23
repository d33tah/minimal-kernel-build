 
#ifndef _ASM_X86_PROCESSOR_H
#define _ASM_X86_PROCESSOR_H

#include <asm/processor-flags.h>

struct task_struct;
struct io_bitmap;

#include <asm/segment.h>
#include <asm/types.h>
#include <uapi/asm/sigcontext.h>
#include <asm/current.h>
#include <asm/cpufeatures.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>
#include <asm/percpu.h>
#include <asm/msr.h>
#include <asm/desc_defs.h>
#include <asm/asm.h>
#include <asm/special_insns.h>
#include <asm/fpu/types.h>
#include <asm/unwind_hints.h>
static __always_inline void rep_nop(void) { asm volatile("rep; nop" ::: "memory"); }
static __always_inline void cpu_relax(void) { rep_nop(); }

/* personality.h inlined */
enum { READ_IMPLIES_EXEC = 0x0400000, };
enum { PER_LINUX = 0x0000, PER_MASK = 0x00ff, };
#define personality(pers)	(pers & PER_MASK)
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/math64.h>
#include <linux/err.h>
#include <linux/irqflags.h>
#include <linux/mem_encrypt.h>

# define ARCH_MIN_TASKALIGN		__alignof__(union fpregs_state)
# define ARCH_MIN_MMSTRUCT_ALIGN	0

struct cpuinfo_x86 {
	__u8			x86;		 
	__u8			x86_vendor;	 
	__u8			x86_model;
	__u8			x86_stepping;
	__u8			x86_virt_bits;
	__u8			x86_phys_bits;
	 
	__u32			extended_cpuid_level;
	 
	int			cpuid_level;
	 
	union {
		__u32		x86_capability[NCAPINTS + NBUGINTS];
		unsigned long	x86_capability_alignment;
	};
	char			x86_vendor_id[16];

	int			x86_cache_alignment;

	unsigned long		loops_per_jiffy;
	u16			x86_clflush_size;
	bool			smt_active;
	unsigned		initialized : 1;
} __randomize_layout;

#define X86_VENDOR_INTEL	0
#define X86_VENDOR_UNKNOWN	0xff

extern struct cpuinfo_x86	boot_cpu_data;
extern struct cpuinfo_x86	new_cpu_data;

extern __u32			cpu_caps_cleared[NCAPINTS + NBUGINTS];
extern __u32			cpu_caps_set[NCAPINTS + NBUGINTS];

#define cpu_info		boot_cpu_data

#define cache_line_size()	(boot_cpu_data.x86_cache_alignment)

extern void early_cpu_init(void);
extern void identify_boot_cpu(void);

static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
				unsigned int *ecx, unsigned int *edx)
{
	 
	asm volatile("cpuid"
	    : "=a" (*eax),
	      "=b" (*ebx),
	      "=c" (*ecx),
	      "=d" (*edx)
	    : "0" (*eax), "2" (*ecx)
	    : "memory");
}

static inline unsigned long read_cr3_pa(void)
{
	return __read_cr3() & CR3_ADDR_MASK;
}

static inline void load_cr3(pgd_t *pgdir)
{
	write_cr3(__sme_pa(pgdir));
}

struct x86_hw_tss {
	unsigned short		back_link, __blh;
	unsigned long		sp0;
	unsigned short		ss0, __ss0h;
	unsigned long		sp1;

	unsigned short		ss1;	 

	unsigned short		__ss1h;
	unsigned long		sp2;
	unsigned short		ss2, __ss2h;
	unsigned long		__cr3;
	unsigned long		ip;
	unsigned long		flags;
	unsigned long		ax;
	unsigned long		cx;
	unsigned long		dx;
	unsigned long		bx;
	unsigned long		sp;
	unsigned long		bp;
	unsigned long		si;
	unsigned long		di;
	unsigned short		es, __esh;
	unsigned short		cs, __csh;
	unsigned short		ss, __ssh;
	unsigned short		ds, __dsh;
	unsigned short		fs, __fsh;
	unsigned short		gs, __gsh;
	unsigned short		ldt, __ldth;
	unsigned short		trace;
	unsigned short		io_bitmap_base;

} __attribute__((packed));

# define __KERNEL_TSS_LIMIT	\
	(offsetof(struct tss_struct, x86_tss) + sizeof(struct x86_hw_tss) - 1)

#define IO_BITMAP_OFFSET_INVALID	(__KERNEL_TSS_LIMIT + 1)

struct entry_stack {
	char	stack[PAGE_SIZE];
};

struct entry_stack_page {
	struct entry_stack stack;
} __aligned(PAGE_SIZE);

struct tss_struct {
	struct x86_hw_tss	x86_tss;
} __aligned(PAGE_SIZE);

DECLARE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw);

struct irq_stack {
	char		stack[IRQ_STACK_SIZE];
} __aligned(IRQ_STACK_SIZE);

DECLARE_PER_CPU(unsigned long, cpu_current_top_of_stack);

DECLARE_PER_CPU(struct irq_stack *, hardirq_stack_ptr);
DECLARE_PER_CPU(struct irq_stack *, softirq_stack_ptr);

struct thread_struct {

	struct desc_struct	tls_array[GDT_ENTRY_TLS_ENTRIES];
	unsigned long		sp0;
	unsigned long		sp;

	unsigned long fs;
	unsigned long gs;

	unsigned long           virtual_dr6;
	 
	unsigned long		cr2;
	unsigned long		trap_nr;
	unsigned long		error_code;
	 
	struct io_bitmap	*io_bitmap;

	unsigned long		iopl_emul;

	unsigned int		iopl_warn:1;
	unsigned int		sig_on_uaccess_err:1;

	u32			pkru;

	struct fpu		fpu;
	 
};

extern void fpu_thread_struct_whitelist(unsigned long *offset, unsigned long *size);

static inline void arch_thread_struct_whitelist(unsigned long *offset,
						unsigned long *size)
{
	fpu_thread_struct_whitelist(offset, size);
}

#define __cpuid			native_cpuid

static inline void load_sp0(unsigned long sp0)
{
	this_cpu_write(cpu_tss_rw.x86_tss.sp0, sp0);
}

static inline void cpuid(unsigned int op,
			 unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = 0;
	__cpuid(eax, ebx, ecx, edx);
}

static inline void cpuid_count(unsigned int op, int count,
			       unsigned int *eax, unsigned int *ebx,
			       unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = count;
	__cpuid(eax, ebx, ecx, edx);
}

static inline unsigned int cpuid_eax(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return eax;
}

extern void select_idle_routine(const struct cpuinfo_x86 *c);

extern int sysenter_setup(void);

extern void cpu_init(void);
extern void cpu_init_exception_handling(void);

# define BASE_PREFETCH		""

static inline void prefetch(const void *x)
{
	alternative_input(BASE_PREFETCH, "prefetchnta %P1",
			  X86_FEATURE_XMM,
			  "m" (*(const char *)x));
}

static __always_inline void prefetchw(const void *x)
{
	alternative_input(BASE_PREFETCH, "prefetchw %P1",
			  X86_FEATURE_3DNOWPREFETCH,
			  "m" (*(const char *)x));
}

static inline void spin_lock_prefetch(const void *x)
{
	prefetchw(x);
}

#define TOP_OF_INIT_STACK ((unsigned long)&init_stack + sizeof(init_stack) - \
			   TOP_OF_KERNEL_STACK_PADDING)

#define task_pt_regs(task) \
({									\
	unsigned long __ptr = (unsigned long)task_stack_page(task);	\
	__ptr += THREAD_SIZE - TOP_OF_KERNEL_STACK_PADDING;		\
	((struct pt_regs *)__ptr) - 1;					\
})

#define INIT_THREAD  {							  \
	.sp0			= TOP_OF_INIT_STACK,			  \
}

extern void start_thread(struct pt_regs *regs, unsigned long new_ip,
					       unsigned long new_sp);

#define __TASK_UNMAPPED_BASE(task_size)	(PAGE_ALIGN(task_size / 3))

extern unsigned long arch_align_stack(unsigned long sp);

void default_idle(void);

#endif  
