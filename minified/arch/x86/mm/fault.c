#include <linux/extable.h>
#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname)
#endif

#include <asm/kdebug.h>

#include <asm/traps.h>
#include <asm/mmu_context.h>

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

	p4d = p4d_offset(pgd, address);
	p4d_k = p4d_offset(pgd_k, address);

	pud = pud_offset(p4d, address);
	pud_k = pud_offset(p4d_k, address);

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

static noinline void pgtable_bad(struct pt_regs *regs, unsigned long error_code,
				 unsigned long address)
{
	oops_end(oops_begin(), regs, SIGKILL);
}

static void sanitize_error_code(unsigned long address,
				unsigned long *error_code)
{
	if (address >= TASK_SIZE_MAX)
		*error_code |= X86_PF_PROT;
}

static void set_signal_archinfo(unsigned long address, unsigned long error_code)
{
	struct task_struct *tsk = current;

	tsk->thread.trap_nr = X86_TRAP_PF;
	tsk->thread.error_code = error_code | X86_PF_USER;
	tsk->thread.cr2 = address;
}

static noinline void page_fault_oops(struct pt_regs *regs,
				     unsigned long error_code,
				     unsigned long address)
{
	unsigned long flags = oops_begin();
	__die("Oops", regs, error_code);
	oops_end(flags, regs, SIGKILL);
}

static noinline void kernelmode_fixup_or_oops(struct pt_regs *regs,
					      unsigned long error_code,
					      unsigned long address, int signal,
					      int si_code, u32 pkey)
{
	WARN_ON_ONCE(user_mode(regs));

	if (fixup_exception(regs, X86_TRAP_PF, error_code, address))
		return;

	page_fault_oops(regs, error_code, address);
}

static void __bad_area_nosemaphore(struct pt_regs *regs,
				   unsigned long error_code,
				   unsigned long address, u32 pkey, int si_code)
{
	if (!user_mode(regs)) {
		kernelmode_fixup_or_oops(regs, error_code, address, SIGSEGV,
					 si_code, pkey);
		return;
	}

	if (!(error_code & X86_PF_USER)) {
		page_fault_oops(regs, error_code, address);
		return;
	}

	local_irq_enable();

	sanitize_error_code(address, &error_code);

	/* fixup_vdso_exception: always false, VDSO disabled */

	set_signal_archinfo(address, error_code);

	force_sig_fault(SIGSEGV, si_code, (void __user *)address);

	local_irq_disable();
}

static noinline void bad_area_nosemaphore(struct pt_regs *regs,
					  unsigned long error_code,
					  unsigned long address)
{
	__bad_area_nosemaphore(regs, error_code, address, 0, SEGV_MAPERR);
}

static void __bad_area(struct pt_regs *regs, unsigned long error_code,
		       unsigned long address, u32 pkey, int si_code)
{
	struct mm_struct *mm = current->mm;

	mmap_read_unlock(mm);

	__bad_area_nosemaphore(regs, error_code, address, pkey, si_code);
}

static noinline void bad_area(struct pt_regs *regs, unsigned long error_code,
			      unsigned long address)
{
	__bad_area(regs, error_code, address, 0, SEGV_MAPERR);
}

static noinline void bad_area_access_error(struct pt_regs *regs,
					   unsigned long error_code,
					   unsigned long address,
					   struct vm_area_struct *vma)
{
	__bad_area(regs, error_code, address, 0, SEGV_ACCERR);
}

static bool fault_in_kernel_space(unsigned long address)
{
	/* X86_32: no vsyscall check needed */
	return address >= TASK_SIZE_MAX;
}

static void do_kern_addr_fault(struct pt_regs *regs,
			       unsigned long hw_error_code,
			       unsigned long address)
{
	if (!(hw_error_code & (X86_PF_RSVD | X86_PF_USER | X86_PF_PROT))) {
		if (vmalloc_fault(address) >= 0)
			return;
	}

	bad_area_nosemaphore(regs, hw_error_code, address);
}
NOKPROBE_SYMBOL(do_kern_addr_fault);

static inline void do_user_addr_fault(struct pt_regs *regs,
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

	if (unlikely((error_code & (X86_PF_USER | X86_PF_INSTR)) ==
		     X86_PF_INSTR)) {
		page_fault_oops(regs, error_code, address);
		return;
	}

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
	} else {
		if (regs->flags & X86_EFLAGS_IF)
			local_irq_enable();
	}

	if (error_code & X86_PF_WRITE)
		flags |= FAULT_FLAG_WRITE;

	if (unlikely(!down_read_trylock(
		    &mm->mmap_lock))) { /* mmap_read_trylock inlined */
		if (!user_mode(regs) && !search_exception_tables(regs->ip)) {
			bad_area_nosemaphore(regs, error_code, address);
			return;
		}
retry:
		mmap_read_lock(mm);
	} else {
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
	if (unlikely((error_code & X86_PF_PK) || (error_code & X86_PF_SGX) ||
		     ((error_code & X86_PF_WRITE) &&
		      !(vma->vm_flags & VM_WRITE)) ||
		     ((!(error_code & X86_PF_WRITE)) &&
		      (error_code & X86_PF_PROT)) ||
		     ((!(error_code & X86_PF_WRITE)) &&
		      !vma_is_accessible(vma)))) {
		bad_area_access_error(regs, error_code, address, vma);
		return;
	}

	fault = handle_mm_fault(vma, address, flags, regs);

	if (unlikely((fault & VM_FAULT_RETRY) &&
		     (fatal_signal_pending(current) ||
		      (user_mode(regs) && signal_pending(current))))) {
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
		kernelmode_fixup_or_oops(regs, error_code, address, 0, 0,
					 ARCH_DEFAULT_PKEY);
		return;
	}

	if (fault & VM_FAULT_OOM) {
		if (!user_mode(regs)) {
			kernelmode_fixup_or_oops(regs, error_code, address,
						 SIGSEGV, SEGV_MAPERR,
						 ARCH_DEFAULT_PKEY);
			return;
		}

	} else {
		if (fault & (VM_FAULT_SIGBUS | VM_FAULT_HWPOISON |
			     VM_FAULT_HWPOISON_LARGE | VM_FAULT_SIGSEGV))
			bad_area_nosemaphore(regs, error_code, address);
		else
			BUG();
	}
}
NOKPROBE_SYMBOL(do_user_addr_fault);

static __always_inline void handle_page_fault(struct pt_regs *regs,
					      unsigned long error_code,
					      unsigned long address)
{
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

	state = irqentry_enter(regs);

	handle_page_fault(regs, error_code, address);

	irqentry_exit(regs, state);
}
