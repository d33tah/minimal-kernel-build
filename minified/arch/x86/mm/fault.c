// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2001, 2002 Andi Kleen, SuSE Labs.
 *  Copyright (C) 2008-2009, Red Hat Inc., Ingo Molnar
 */
#include <asm/kdebug.h>
#include <linux/extable.h> /* search_exception_tables	*/

#include <asm/traps.h> /* dotraplinkage, ...		*/

static noinline void page_fault_oops(struct pt_regs *regs,
				     unsigned long error_code,
				     unsigned long address)
{
	unsigned long flags;
	int sig;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice:
	 */
	flags = oops_begin();

	sig = SIGKILL;

	oops_end(flags, regs, sig);
}

static noinline void kernelmode_fixup_or_oops(struct pt_regs *regs,
					      unsigned long error_code,
					      unsigned long address)
{
	/* Are we prepared to handle this kernel fault? */
	if (fixup_exception(regs, X86_TRAP_PF, error_code, address)) {
		/*
		 * Any interrupt that takes a fault gets the fixup. This makes
		 * the below recursive fault logic only apply to a faults from
		 * task context.
		 */
		if (in_interrupt())
			return;

		/*
		 * Barring that, we can do the fixup and be happy.
		 */
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

	/*
	 * Only writes to RO or instruction fetches from NX may cause
	 * spurious faults.
	 *
	 * These could be from user or supervisor accesses but the TLB
	 * is only lazily flushed after a kernel mapping protection
	 * change, so user accesses are not expected to cause spurious
	 * faults.
	 */
	if (error_code != (X86_PF_WRITE | X86_PF_PROT) &&
	    error_code != (X86_PF_INSTR | X86_PF_PROT))
		return 0;

	pgd = init_mm.pgd + pgd_index(address);

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

	/*
	 * Make sure we have permissions in PMD.
	 * If not, then there's a bug in the page tables:
	 */
	ret = spurious_kernel_fault_check(error_code, (pte_t *)pmd);
	return ret;
}

static inline int access_error(unsigned long error_code,
			       struct vm_area_struct *vma)
{
	if (error_code & X86_PF_WRITE) {
		/* write, present and write, not present: */
		if (unlikely(!(vma->vm_flags & VM_WRITE)))
			return 1;
		return 0;
	}

	/* read, present: */
	if (unlikely(error_code & X86_PF_PROT))
		return 1;

	/* read, not present: */
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
	/* Was the fault spurious, caused by lazy TLB invalidation? */
	if (spurious_kernel_fault(hw_error_code, address))
		return;

	/*
	 * Note, despite being a "bad area", there are quite a few
	 * acceptable reasons to get here, such as erratum fixups
	 * and handling kernel code that can fault, like get_user().
	 *
	 * Don't take the mm semaphore here. If we fixup a prefetch
	 * fault we could otherwise deadlock:
	 */
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
		/*
		 * No extable entry here.  This was a kernel access to an
		 * invalid pointer.  get_kernel_nofault() will not get here.
		 */
		page_fault_oops(regs, error_code, address);
		return;
	}

	/*
	 * If we're in an interrupt, have no user context or are running
	 * in a region with pagefaults disabled then we must not take the fault
	 */
	if (unlikely(faulthandler_disabled() || !mm)) {
		bad_area_nosemaphore(regs, error_code, address);
		return;
	}

	/*
	 * It's safe to allow irq's after cr2 has been saved and the
	 * vmalloc fault has been handled.
	 *
	 * User-mode registers count as a user access even for any
	 * potential system fault or CPU buglet:
	 */
	if (user_mode(regs)) {
		local_irq_enable();
	} else {
		if (regs->flags & X86_EFLAGS_IF)
			local_irq_enable();
	}

	if (error_code & X86_PF_WRITE)
		flags |= FAULT_FLAG_WRITE;

	/*
	 * Kernel-mode access to the user address space should only occur
	 * on well-defined single instructions listed in the exception
	 * tables.  But, an erroneous kernel fault occurring outside one of
	 * those areas which also holds mmap_lock might deadlock attempting
	 * to validate the fault against the address space.
	 *
	 * Only do the expensive exception table search when we might be at
	 * risk of a deadlock.  This happens if we
	 * 1. Failed to acquire mmap_lock, and
	 * 2. The access did not originate in userspace.
	 */
	if (unlikely(!mmap_read_trylock(mm))) {
		if (!user_mode(regs) && !search_exception_tables(regs->ip)) {
			/*
			 * Fault from code in kernel from
			 * which we do not expect faults.
			 */
			bad_area_nosemaphore(regs, error_code, address);
			return;
		}
retry:
		mmap_read_lock(mm);
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

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
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

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.  Since we never set FAULT_FLAG_RETRY_NOWAIT, if
	 * we get VM_FAULT_RETRY back, the mmap_lock has been unlocked.
	 *
	 * Note that handle_userfault() may also release and reacquire mmap_lock
	 * (and not return with VM_FAULT_RETRY), when returning to userland to
	 * repeat the page fault later with a VM_FAULT_NOPAGE retval
	 * (potentially after handling any pending signal during the return to
	 * userland). The return to userland is identified whenever
	 * FAULT_FLAG_USER|FAULT_FLAG_KILLABLE are both set in flags.
	 */
	fault = handle_mm_fault(vma, address, flags, regs);

	/*
	 * If we need to retry the mmap_lock has already been released,
	 * and if there is a fatal signal pending there is no guarantee
	 * that we made any progress. Handle this case first.
	 */
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
	/* Was the fault on kernel-controlled part of the address space? */
	if (unlikely(fault_in_kernel_space(address))) {
		do_kern_addr_fault(regs, error_code, address);
	} else {
		do_user_addr_fault(regs, error_code, address);

		/*
		 * User address page fault handling might have reenabled
		 * interrupts. Fixing up all potential exit points of
		 * do_user_addr_fault() and its leaf functions is just not
		 * doable w/o creating an unholy mess or turning the code
		 * upside down.
		 */
		local_irq_disable();
	}
}

DEFINE_IDTENTRY_RAW_ERRORCODE(exc_page_fault)
{
	unsigned long address = read_cr2();
	irqentry_state_t state;

	/*
	 * Entry handling for valid #PF from kernel mode is slightly
	 * different: RCU is already watching and rcu_irq_enter() must not
	 * be invoked because a kernel fault on a user space address might
	 * sleep.
	 *
	 * In case the fault hit a RCU idle region the conditional entry
	 * code reenabled RCU to avoid subsequent wreckage which helps
	 * debuggability.
	 */
	state = irqentry_enter(regs);

	handle_page_fault(regs, error_code, address);

	irqentry_exit(regs, state);
}
