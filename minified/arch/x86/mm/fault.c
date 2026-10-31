#include <linux/extable.h>

#include <asm/traps.h>
/* Removed: #include <asm/kvm_para.h> - stub below */

/*
 * arch_sync_kernel_mappings + its private helper vmalloc_sync_one were
 * runtime-dead (anchor-stub): the vmalloc page-table sync machinery in
 * mm/vmalloc.c that called this was already stripped, leaving zero callers
 * tree-wide.  Removed whole (along with pgd_page_get_mm in pgtable.c, whose
 * sole caller was vmalloc_sync_one's loop).
 */

static noinline void page_fault_oops(struct pt_regs *regs,
				     unsigned long error_code,
				     unsigned long address)
{
	/* Anchor-stub: unhandled-kernel-fault oops path, runtime-dead in a
	 * healthy boot. Kept link-live for its callers in this file. */
}

static noinline void kernelmode_fixup_or_oops(struct pt_regs *regs,
					      unsigned long error_code,
					      unsigned long address)
{
	WARN_ON_ONCE(user_mode(regs));

	if (fixup_exception(regs, X86_TRAP_PF, error_code, address)) {
		if (in_interrupt())
			return;

		/* sig_on_uaccess_err branch removed - field was never set (always 0) */

		return;
	}

	page_fault_oops(regs, error_code, address);
}

static noinline void bad_area_nosemaphore(struct pt_regs *regs,
					  unsigned long error_code,
					  unsigned long address)
{
	if (!user_mode(regs)) {
		kernelmode_fixup_or_oops(regs, error_code, address);
		return;
	}

	if (!(error_code & X86_PF_USER))
		page_fault_oops(regs, error_code, address);
	/* No signal delivery: kill the task instead of re-faulting forever. */
	do_exit(SIGSEGV);
}

static noinline void bad_area(struct pt_regs *regs, unsigned long error_code,
			      unsigned long address)
{
	mmap_read_unlock(current->mm);
	bad_area_nosemaphore(regs, error_code, address);
}

static int spurious_kernel_fault_check(unsigned long error_code, pte_t *pte)
{
	if ((error_code & X86_PF_WRITE) && !pte_write(*pte))
		return 0;

	if ((error_code & X86_PF_INSTR) && !pte_exec(*pte))
		return 0;

	return 1;
}

static noinline int spurious_kernel_fault(unsigned long error_code,
					  unsigned long address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int ret;

	if (error_code != (X86_PF_WRITE | X86_PF_PROT) &&
	    error_code != (X86_PF_INSTR | X86_PF_PROT))
		return 0;

	pgd = init_mm.pgd + pgd_index(address);

	/*
	 * 2-level x86_32 (P4D/PUD/PMD folded): pgd_present/p4d_present/
	 * pud_present are constant 1 and p4d_large/pud_large are constant 0,
	 * so every upper-level check and leaf branch above the PMD was dead.
	 * Descend straight to the (real) PMD entry.
	 */
	pmd = pmd_offset(pud_offset(p4d_offset(pgd, address), address),
			 address);
	if (!pmd_present(*pmd))
		return 0;

	if (pmd_large(*pmd))
		return spurious_kernel_fault_check(error_code, (pte_t *)pmd);

	pte = pte_offset_kernel(pmd, address);
	if (!pte_present(*pte))
		return 0;

	ret = spurious_kernel_fault_check(error_code, pte);
	if (!ret)
		return 0;

	ret = spurious_kernel_fault_check(error_code, (pte_t *)pmd);
	WARN_ONCE(!ret, "PMD has incorrect permission bits\n");

	return ret;
}

static inline int access_error(unsigned long error_code,
			       struct vm_area_struct *vma)
{
	/*
	 * X86_PF_PK (protection-key violation) error-code bit is never set on
	 * this build: PKU/OSPKE are compile-disabled (CR4.PKE never set, no
	 * setup_pku). That guard was statically dead.
	 */

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

static bool fault_in_kernel_space(unsigned long address)
{
	return address >= TASK_SIZE_MAX;
}

static void do_kern_addr_fault(struct pt_regs *regs,
			       unsigned long hw_error_code,
			       unsigned long address)
{
	WARN_ON_ONCE(hw_error_code & X86_PF_PK);

	if (spurious_kernel_fault(hw_error_code, address))
		return;

	bad_area_nosemaphore(regs, hw_error_code, address);
}

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

	if (unlikely(!mmap_read_trylock(mm))) {
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
	bad_area(regs, error_code, address);
	return;

good_area:
	if (unlikely(access_error(error_code, vma))) {
		/*
		 * Protection keys are compile-time disabled (OSPKE in
		 * DISABLED_MASK), so a fault can never be a pkey access error --
		 * always SEGV_ACCERR.
		 */
		bad_area(regs, error_code, address);
		return;
	}

	fault = handle_mm_fault(vma, address, flags, regs);

	if (unlikely(fault & VM_FAULT_RETRY)) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}

	mmap_read_unlock(mm);
	if (likely(!(fault & VM_FAULT_ERROR)))
		return;

	if (fault & (VM_FAULT_OOM | VM_FAULT_SIGBUS | VM_FAULT_HWPOISON |
		     VM_FAULT_HWPOISON_LARGE)) {
		if (!user_mode(regs))
			kernelmode_fixup_or_oops(regs, error_code, address);
		else
			do_exit(SIGBUS); /* no signals or OOM killer */
	} else if (fault & VM_FAULT_SIGSEGV)
		bad_area_nosemaphore(regs, error_code, address);
	else
		BUG();
}

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
