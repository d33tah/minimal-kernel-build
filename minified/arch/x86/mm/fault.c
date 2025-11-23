 
 
#include <linux/sched.h>		 
#include <linux/sched/task_stack.h>	 
#include <linux/kdebug.h>		 
#include <linux/extable.h>		 
#include <linux/memblock.h>		 
#include <linux/kfence.h>		 
#include <linux/kprobes.h>		 
#include <linux/mmiotrace.h>		 
#include <linux/perf_event.h>		 
#include <linux/hugetlb.h>		 
		 
#include <linux/context_tracking.h>	 
#include <linux/uaccess.h>		 
#include <linux/efi.h>			 
#include <linux/mm_types.h>

#include <asm/cpufeature.h>		 
#include <asm/traps.h>			 
#include <asm/fixmap.h>			 
#include <asm/vsyscall.h>		 
#include <asm/vm86.h>			 
#include <asm/mmu_context.h>		 
#include <asm/efi.h>			 
#include <asm/desc.h>			 
#include <asm/cpu_entry_area.h>		 
#include <asm/pgtable_areas.h>		 
#include <asm/kvm_para.h>		 
#include <asm/vdso.h>			 
#include <asm/irq_stack.h>

#include <asm/trace/exceptions.h>

 
static nokprobe_inline int
kmmio_fault(struct pt_regs *regs, unsigned long addr)
{
	if (unlikely(is_kmmio_active()))
		if (kmmio_handler(regs, addr) == 1)
			return -1;
	return 0;
}

 
static inline int
check_prefetch_opcode(struct pt_regs *regs, unsigned char *instr,
		      unsigned char opcode, int *prefetch)
{
	unsigned char instr_hi = opcode & 0xf0;
	unsigned char instr_lo = opcode & 0x0f;

	switch (instr_hi) {
	case 0x20:
	case 0x30:
		 
		return ((instr_lo & 7) == 0x6);
	case 0x60:
		 
		return (instr_lo & 0xC) == 0x4;
	case 0xF0:
		 
		return !instr_lo || (instr_lo>>1) == 1;
	case 0x00:
		 
		if (get_kernel_nofault(opcode, instr))
			return 0;

		*prefetch = (instr_lo == 0xF) &&
			(opcode == 0x0D || opcode == 0x18);
		return 0;
	default:
		return 0;
	}
}

static bool is_amd_k8_pre_npt(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	return unlikely(IS_ENABLED(CONFIG_CPU_SUP_AMD) &&
			c->x86_vendor == X86_VENDOR_AMD &&
			c->x86 == 0xf && c->x86_model < 0x40);
}

static int
is_prefetch(struct pt_regs *regs, unsigned long error_code, unsigned long addr)
{
	unsigned char *max_instr;
	unsigned char *instr;
	int prefetch = 0;

	 
	if (!is_amd_k8_pre_npt())
		return 0;

	 
	if (error_code & X86_PF_INSTR)
		return 0;

	instr = (void *)convert_ip_to_linear(current, regs);
	max_instr = instr + 15;

	 
	pagefault_disable();

	while (instr < max_instr) {
		unsigned char opcode;

		if (user_mode(regs)) {
			if (get_user(opcode, (unsigned char __user *) instr))
				break;
		} else {
			if (get_kernel_nofault(opcode, instr))
				break;
		}

		instr++;

		if (!check_prefetch_opcode(regs, instr, opcode, &prefetch))
			break;
	}

	pagefault_enable();
	return prefetch;
}

DEFINE_SPINLOCK(pgd_lock);
LIST_HEAD(pgd_list);

static inline pmd_t *vmalloc_sync_one(pgd_t *pgd, unsigned long address)
{
	unsigned index = pgd_index(address);
	pgd_t *pgd_k;
	p4d_t *p4d, *p4d_k;
	pud_t *pud, *pud_k;
	pmd_t *pmd, *pmd_k;

	pgd += index;
	pgd_k = init_mm.pgd + index;

	if (!pgd_present(*pgd_k))
		return NULL;

	 
	p4d = p4d_offset(pgd, address);
	p4d_k = p4d_offset(pgd_k, address);
	if (!p4d_present(*p4d_k))
		return NULL;

	pud = pud_offset(p4d, address);
	pud_k = pud_offset(p4d_k, address);
	if (!pud_present(*pud_k))
		return NULL;

	pmd = pmd_offset(pud, address);
	pmd_k = pmd_offset(pud_k, address);

	if (pmd_present(*pmd) != pmd_present(*pmd_k))
		set_pmd(pmd, *pmd_k);

	if (!pmd_present(*pmd_k))
		return NULL;
	else
		BUG_ON(pmd_pfn(*pmd) != pmd_pfn(*pmd_k));

	return pmd_k;
}

 
static noinline int vmalloc_fault(unsigned long address)
{
	unsigned long pgd_paddr;
	pmd_t *pmd_k;
	pte_t *pte_k;

	 
	if (!(address >= VMALLOC_START && address < VMALLOC_END))
		return -1;

	 
	pgd_paddr = read_cr3_pa();
	pmd_k = vmalloc_sync_one(__va(pgd_paddr), address);
	if (!pmd_k)
		return -1;

	if (pmd_large(*pmd_k))
		return 0;

	pte_k = pte_offset_kernel(pmd_k, address);
	if (!pte_present(*pte_k))
		return -1;

	return 0;
}
NOKPROBE_SYMBOL(vmalloc_fault);

void arch_sync_kernel_mappings(unsigned long start, unsigned long end)
{
	unsigned long addr;

	for (addr = start & PMD_MASK;
	     addr >= TASK_SIZE_MAX && addr < VMALLOC_END;
	     addr += PMD_SIZE) {
		struct page *page;

		spin_lock(&pgd_lock);
		list_for_each_entry(page, &pgd_list, lru) {
			spinlock_t *pgt_lock;

			 
			pgt_lock = &pgd_page_get_mm(page)->page_table_lock;

			spin_lock(pgt_lock);
			vmalloc_sync_one(page_address(page), addr);
			spin_unlock(pgt_lock);
		}
		spin_unlock(&pgd_lock);
	}
}

static int is_errata93(struct pt_regs *regs, unsigned long address)
{
	return 0;
}

 
static int is_errata100(struct pt_regs *regs, unsigned long address)
{
	return 0;
}

 
static int is_f00f_bug(struct pt_regs *regs, unsigned long error_code,
		       unsigned long address)
{
	return 0;
}

static noinline void
pgtable_bad(struct pt_regs *regs, unsigned long error_code,
	    unsigned long address)
{
	/* Simplified: just die without verbose output */
	oops_end(oops_begin(), regs, SIGKILL);
}

static void sanitize_error_code(unsigned long address,
				unsigned long *error_code)
{
	 
	if (address >= TASK_SIZE_MAX)
		*error_code |= X86_PF_PROT;
}

static void set_signal_archinfo(unsigned long address,
				unsigned long error_code)
{
	struct task_struct *tsk = current;

	tsk->thread.trap_nr = X86_TRAP_PF;
	tsk->thread.error_code = error_code | X86_PF_USER;
	tsk->thread.cr2 = address;
}

static noinline void
page_fault_oops(struct pt_regs *regs, unsigned long error_code,
		unsigned long address)
{
	/* Simplified: just die with minimal output */
	unsigned long flags = oops_begin();
	int sig = SIGKILL;
	if (__die("Oops", regs, error_code))
		sig = 0;
	oops_end(flags, regs, sig);
}

static noinline void
kernelmode_fixup_or_oops(struct pt_regs *regs, unsigned long error_code,
			 unsigned long address, int signal, int si_code,
			 u32 pkey)
{
	WARN_ON_ONCE(user_mode(regs));

	 
	if (fixup_exception(regs, X86_TRAP_PF, error_code, address)) {
		 
		if (in_interrupt())
			return;

		 
		if (current->thread.sig_on_uaccess_err && signal) {
			sanitize_error_code(address, &error_code);

			set_signal_archinfo(address, error_code);

			if (si_code == SEGV_PKUERR) {
				force_sig_pkuerr((void __user *)address, pkey);
			} else {
				 
				force_sig_fault(signal, si_code, (void __user *)address);
			}
		}

		 
		return;
	}

	 
	if (is_prefetch(regs, error_code, address))
		return;

	page_fault_oops(regs, error_code, address);
}

 
static inline void
show_signal_msg(struct pt_regs *regs, unsigned long error_code,
		unsigned long address, struct task_struct *tsk)
{
	/* Stub: verbose segfault messages not needed for minimal kernel */
}

 
static bool is_vsyscall_vaddr(unsigned long vaddr)
{
	return unlikely((vaddr & PAGE_MASK) == VSYSCALL_ADDR);
}

static void
__bad_area_nosemaphore(struct pt_regs *regs, unsigned long error_code,
		       unsigned long address, u32 pkey, int si_code)
{
	struct task_struct *tsk = current;

	if (!user_mode(regs)) {
		kernelmode_fixup_or_oops(regs, error_code, address,
					 SIGSEGV, si_code, pkey);
		return;
	}

	if (!(error_code & X86_PF_USER)) {
		 
		page_fault_oops(regs, error_code, address);
		return;
	}

	 
	local_irq_enable();

	 
	if (is_prefetch(regs, error_code, address))
		return;

	if (is_errata100(regs, address))
		return;

	sanitize_error_code(address, &error_code);

	if (fixup_vdso_exception(regs, X86_TRAP_PF, error_code, address))
		return;

	if (likely(show_unhandled_signals))
		show_signal_msg(regs, error_code, address, tsk);

	set_signal_archinfo(address, error_code);

	if (si_code == SEGV_PKUERR)
		force_sig_pkuerr((void __user *)address, pkey);
	else
		force_sig_fault(SIGSEGV, si_code, (void __user *)address);

	local_irq_disable();
}

static noinline void
bad_area_nosemaphore(struct pt_regs *regs, unsigned long error_code,
		     unsigned long address)
{
	__bad_area_nosemaphore(regs, error_code, address, 0, SEGV_MAPERR);
}

static void
__bad_area(struct pt_regs *regs, unsigned long error_code,
	   unsigned long address, u32 pkey, int si_code)
{
	struct mm_struct *mm = current->mm;
	 
	mmap_read_unlock(mm);

	__bad_area_nosemaphore(regs, error_code, address, pkey, si_code);
}

static noinline void
bad_area(struct pt_regs *regs, unsigned long error_code, unsigned long address)
{
	__bad_area(regs, error_code, address, 0, SEGV_MAPERR);
}

static inline bool bad_area_access_from_pkeys(unsigned long error_code,
		struct vm_area_struct *vma)
{
	 
	bool foreign = false;

	if (!cpu_feature_enabled(X86_FEATURE_OSPKE))
		return false;
	if (error_code & X86_PF_PK)
		return true;
	 
	if (!arch_vma_access_permitted(vma, (error_code & X86_PF_WRITE),
				       (error_code & X86_PF_INSTR), foreign))
		return true;
	return false;
}

static noinline void
bad_area_access_error(struct pt_regs *regs, unsigned long error_code,
		      unsigned long address, struct vm_area_struct *vma)
{
	 
	if (bad_area_access_from_pkeys(error_code, vma)) {
		 
		u32 pkey = vma_pkey(vma);

		__bad_area(regs, error_code, address, pkey, SEGV_PKUERR);
	} else {
		__bad_area(regs, error_code, address, 0, SEGV_ACCERR);
	}
}

static void
do_sigbus(struct pt_regs *regs, unsigned long error_code, unsigned long address,
	  vm_fault_t fault)
{
	 
	if (!user_mode(regs)) {
		kernelmode_fixup_or_oops(regs, error_code, address,
					 SIGBUS, BUS_ADRERR, ARCH_DEFAULT_PKEY);
		return;
	}

	 
	if (is_prefetch(regs, error_code, address))
		return;

	sanitize_error_code(address, &error_code);

	if (fixup_vdso_exception(regs, X86_TRAP_PF, error_code, address))
		return;

	set_signal_archinfo(address, error_code);

	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);
}

static int spurious_kernel_fault_check(unsigned long error_code, pte_t *pte)
{
	if ((error_code & X86_PF_WRITE) && !pte_write(*pte))
		return 0;

	if ((error_code & X86_PF_INSTR) && !pte_exec(*pte))
		return 0;

	return 1;
}

 
static noinline int
spurious_kernel_fault(unsigned long error_code, unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int ret;

	 
	if (error_code != (X86_PF_WRITE | X86_PF_PROT) &&
	    error_code != (X86_PF_INSTR | X86_PF_PROT))
		return 0;

	pgd = init_mm.pgd + pgd_index(address);
	if (!pgd_present(*pgd))
		return 0;

	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		return 0;

	if (p4d_large(*p4d))
		return spurious_kernel_fault_check(error_code, (pte_t *) p4d);

	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		return 0;

	if (pud_large(*pud))
		return spurious_kernel_fault_check(error_code, (pte_t *) pud);

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		return 0;

	if (pmd_large(*pmd))
		return spurious_kernel_fault_check(error_code, (pte_t *) pmd);

	pte = pte_offset_kernel(pmd, address);
	if (!pte_present(*pte))
		return 0;

	ret = spurious_kernel_fault_check(error_code, pte);
	if (!ret)
		return 0;

	 
	ret = spurious_kernel_fault_check(error_code, (pte_t *) pmd);
	WARN_ONCE(!ret, "PMD has incorrect permission bits\n");

	return ret;
}
NOKPROBE_SYMBOL(spurious_kernel_fault);

int show_unhandled_signals = 1;

static inline int
access_error(unsigned long error_code, struct vm_area_struct *vma)
{
	 
	bool foreign = false;

	 
	if (error_code & X86_PF_PK)
		return 1;

	 
	if (unlikely(error_code & X86_PF_SGX))
		return 1;

	 
	if (!arch_vma_access_permitted(vma, (error_code & X86_PF_WRITE),
				       (error_code & X86_PF_INSTR), foreign))
		return 1;

	if (error_code & X86_PF_WRITE) {
		 
		if (unlikely(!(vma->vm_flags & VM_WRITE)))
			return 1;
		return 0;
	}

	 
	if (unlikely(error_code & X86_PF_PROT))
		return 1;

	 
	if (unlikely(!vma_is_accessible(vma)))
		return 1;

	return 0;
}

bool fault_in_kernel_space(unsigned long address)
{
	 
	if (IS_ENABLED(CONFIG_X86_64) && is_vsyscall_vaddr(address))
		return false;

	return address >= TASK_SIZE_MAX;
}

 
static void
do_kern_addr_fault(struct pt_regs *regs, unsigned long hw_error_code,
		   unsigned long address)
{
	 
	WARN_ON_ONCE(hw_error_code & X86_PF_PK);

	 
	if (!(hw_error_code & (X86_PF_RSVD | X86_PF_USER | X86_PF_PROT))) {
		if (vmalloc_fault(address) >= 0)
			return;
	}

	if (is_f00f_bug(regs, hw_error_code, address))
		return;

	 
	if (spurious_kernel_fault(hw_error_code, address))
		return;

	 
	if (WARN_ON_ONCE(kprobe_page_fault(regs, X86_TRAP_PF)))
		return;

	 
	bad_area_nosemaphore(regs, hw_error_code, address);
}
NOKPROBE_SYMBOL(do_kern_addr_fault);

 
static inline
void do_user_addr_fault(struct pt_regs *regs,
			unsigned long error_code,
			unsigned long address)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk;
	struct mm_struct *mm;
	vm_fault_t fault;
	unsigned int flags = FAULT_FLAG_DEFAULT;

	tsk = current;
	mm = tsk->mm;

	if (unlikely((error_code & (X86_PF_USER | X86_PF_INSTR)) == X86_PF_INSTR)) {
		 
		if (is_errata93(regs, address))
			return;

		page_fault_oops(regs, error_code, address);
		return;
	}

	 
	if (WARN_ON_ONCE(kprobe_page_fault(regs, X86_TRAP_PF)))
		return;

	 
	if (unlikely(error_code & X86_PF_RSVD))
		pgtable_bad(regs, error_code, address);

	 
	if (unlikely(cpu_feature_enabled(X86_FEATURE_SMAP) &&
		     !(error_code & X86_PF_USER) &&
		     !(regs->flags & X86_EFLAGS_AC))) {
		 
		page_fault_oops(regs, error_code, address);
		return;
	}

	 
	if (unlikely(faulthandler_disabled() || !mm)) {
		bad_area_nosemaphore(regs, error_code, address);
		return;
	}

	 
	if (user_mode(regs)) {
		local_irq_enable();
		flags |= FAULT_FLAG_USER;
	} else {
		if (regs->flags & X86_EFLAGS_IF)
			local_irq_enable();
	}

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);

	if (error_code & X86_PF_WRITE)
		flags |= FAULT_FLAG_WRITE;
	if (error_code & X86_PF_INSTR)
		flags |= FAULT_FLAG_INSTRUCTION;


	 
	if (unlikely(!mmap_read_trylock(mm))) {
		if (!user_mode(regs) && !search_exception_tables(regs->ip)) {
			 
			bad_area_nosemaphore(regs, error_code, address);
			return;
		}
retry:
		mmap_read_lock(mm);
	} else {
		 
		might_sleep();
	}

	vma = find_vma(mm, address);
	if (unlikely(!vma)) {
		bad_area(regs, error_code, address);
		return;
	}
	if (likely(vma->vm_start <= address))
		goto good_area;
	if (unlikely(!(vma->vm_flags & VM_GROWSDOWN))) {
		bad_area(regs, error_code, address);
		return;
	}
	if (unlikely(expand_stack(vma, address))) {
		bad_area(regs, error_code, address);
		return;
	}

	 
good_area:
	if (unlikely(access_error(error_code, vma))) {
		bad_area_access_error(regs, error_code, address, vma);
		return;
	}

	 
	fault = handle_mm_fault(vma, address, flags, regs);

	if (fault_signal_pending(fault, regs)) {
		 
		if (!user_mode(regs))
			kernelmode_fixup_or_oops(regs, error_code, address,
						 SIGBUS, BUS_ADRERR,
						 ARCH_DEFAULT_PKEY);
		return;
	}

	 
	if (unlikely(fault & VM_FAULT_RETRY)) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}

	mmap_read_unlock(mm);
	if (likely(!(fault & VM_FAULT_ERROR)))
		return;

	if (fatal_signal_pending(current) && !user_mode(regs)) {
		kernelmode_fixup_or_oops(regs, error_code, address,
					 0, 0, ARCH_DEFAULT_PKEY);
		return;
	}

	if (fault & VM_FAULT_OOM) {
		 
		if (!user_mode(regs)) {
			kernelmode_fixup_or_oops(regs, error_code, address,
						 SIGSEGV, SEGV_MAPERR,
						 ARCH_DEFAULT_PKEY);
			return;
		}

		 
		pagefault_out_of_memory();
	} else {
		if (fault & (VM_FAULT_SIGBUS|VM_FAULT_HWPOISON|
			     VM_FAULT_HWPOISON_LARGE))
			do_sigbus(regs, error_code, address, fault);
		else if (fault & VM_FAULT_SIGSEGV)
			bad_area_nosemaphore(regs, error_code, address);
		else
			BUG();
	}
}
NOKPROBE_SYMBOL(do_user_addr_fault);

static __always_inline void
trace_page_fault_entries(struct pt_regs *regs, unsigned long error_code,
			 unsigned long address)
{
	if (!trace_pagefault_enabled())
		return;

	if (user_mode(regs))
		trace_page_fault_user(address, regs, error_code);
	else
		trace_page_fault_kernel(address, regs, error_code);
}

static __always_inline void
handle_page_fault(struct pt_regs *regs, unsigned long error_code,
			      unsigned long address)
{
	trace_page_fault_entries(regs, error_code, address);

	if (unlikely(kmmio_fault(regs, address)))
		return;

	 
	if (unlikely(fault_in_kernel_space(address))) {
		do_kern_addr_fault(regs, error_code, address);
	} else {
		do_user_addr_fault(regs, error_code, address);
		 
		local_irq_disable();
	}
}

DEFINE_IDTENTRY_RAW_ERRORCODE(exc_page_fault)
{
	unsigned long address = read_cr2();
	irqentry_state_t state;

	prefetchw(&current->mm->mmap_lock);

	 
	if (kvm_handle_async_pf(regs, (u32)address))
		return;

	 
	state = irqentry_enter(regs);

	handle_page_fault(regs, error_code, address);

	irqentry_exit(regs, state);
}
