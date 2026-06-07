#include <linux/extable.h>
#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname)
#endif

#include <asm/kdebug.h>

#include <asm/traps.h>
#include <asm/mmu_context.h>

DEFINE_SPINLOCK(pgd_lock);
LIST_HEAD(pgd_list);

static noinline int vmalloc_fault(unsigned long address)
{
	return -1;
}
NOKPROBE_SYMBOL(vmalloc_fault);

static noinline void pgtable_bad(struct pt_regs *regs, unsigned long error_code,
				 unsigned long address)
{
	oops_end(oops_begin(), regs, SIGKILL);
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
					      unsigned long address)
{
	if (fixup_exception(regs, X86_TRAP_PF, error_code, address))
		return;

	page_fault_oops(regs, error_code, address);
}

static void __bad_area_nosemaphore(struct pt_regs *regs,
				   unsigned long error_code,
				   unsigned long address)
{
	if (!user_mode(regs)) {
		kernelmode_fixup_or_oops(regs, error_code, address);
		return;
	}

	/* User-mode fault: force_sig_fault is stubbed to panic() */
	page_fault_oops(regs, error_code, address);
}

static noinline void bad_area(struct pt_regs *regs, unsigned long error_code,
			      unsigned long address)
{
	mmap_read_unlock(current->mm);
	__bad_area_nosemaphore(regs, error_code, address);
}

static void do_kern_addr_fault(struct pt_regs *regs,
			       unsigned long hw_error_code,
			       unsigned long address)
{
	if (!(hw_error_code & (X86_PF_RSVD | X86_PF_USER | X86_PF_PROT))) {
		if (vmalloc_fault(address) >= 0)
			return;
	}

	__bad_area_nosemaphore(regs, hw_error_code, address);
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
		__bad_area_nosemaphore(regs, error_code, address);
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
			__bad_area_nosemaphore(regs, error_code, address);
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
		bad_area(regs, error_code, address);
		return;
	}

	fault = handle_mm_fault(vma, address, flags, regs);

	if (unlikely((fault & VM_FAULT_RETRY) &&
		     (fatal_signal_pending(current) ||
		      (user_mode(regs) && signal_pending(current))))) {
		if (!user_mode(regs))
			kernelmode_fixup_or_oops(regs, error_code, address);
		return;
	}

	if (unlikely(fault & VM_FAULT_RETRY)) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}

	mmap_read_unlock(mm);
	if (likely(!(fault & VM_FAULT_ERROR)))
		return;

	if (!user_mode(regs)) {
		kernelmode_fixup_or_oops(regs, error_code, address);
		return;
	}

	/* User-mode fault error: all paths lead to panic */
	page_fault_oops(regs, error_code, address);
}
NOKPROBE_SYMBOL(do_user_addr_fault);

static __always_inline void handle_page_fault(struct pt_regs *regs,
					      unsigned long error_code,
					      unsigned long address)
{
	if (unlikely(address >= TASK_SIZE_MAX)) {
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
