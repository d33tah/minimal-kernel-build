#include <linux/mm.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/cpu.h>
#include <linux/ptrace.h>
#include <linux/time_namespace.h>

#include <asm/pvclock.h>
#include <asm/vgtod.h>
#include <asm/proto.h>
#include <asm/vdso.h>
#include <asm/tlb.h>
#include <asm/page.h>
#include <asm/desc.h>
#include <asm/cpufeature.h>

/* Inlined from clocksource/hyperv_timer.h - only needed for hv_get_tsc_page stub */
struct ms_hyperv_tsc_page;
static inline struct ms_hyperv_tsc_page *hv_get_tsc_page(void)
{
	return NULL;
}

#undef _ASM_X86_VVAR_H
#define EMIT_VVAR(name, offset) const size_t name##_offset = offset;
#include <asm/vvar.h>

struct vdso_data *arch_get_vdso_data(void *vvar_page)
{
	return (struct vdso_data *)(vvar_page + _vdso_data_offset);
}
#undef EMIT_VVAR

unsigned int vclocks_used __read_mostly;

void __init init_vdso_image(const struct vdso_image *image)
{
	BUG_ON(image->size % PAGE_SIZE != 0);

	apply_alternatives((struct alt_instr *)(image->data + image->alt),
			   (struct alt_instr *)(image->data + image->alt +
						image->alt_len));
}

static const struct vm_special_mapping vvar_mapping;
struct linux_binprm;

static vm_fault_t vdso_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	const struct vdso_image *image = vma->vm_mm->context.vdso_image;

	if (!image || (vmf->pgoff << PAGE_SHIFT) >= image->size)
		return VM_FAULT_SIGBUS;

	vmf->page = virt_to_page(image->data + (vmf->pgoff << PAGE_SHIFT));
	get_page(vmf->page);
	return 0;
}

static void vdso_fix_landing(const struct vdso_image *image,
			     struct vm_area_struct *new_vma)
{
	if (in_ia32_syscall() && image == &vdso_image_32) {
		struct pt_regs *regs = current_pt_regs();
		unsigned long vdso_land = image->sym_int80_landing_pad;
		unsigned long old_land_addr =
			vdso_land + (unsigned long)current->mm->context.vdso;

		if (regs->ip == old_land_addr)
			regs->ip = new_vma->vm_start + vdso_land;
	}
}

/* vdso_mremap removed - mremap syscall returns ENOSYS */

/* find_timens_vvar_page removed - always returns NULL, timens code simplified */

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	const struct vdso_image *image = vma->vm_mm->context.vdso_image;
	unsigned long pfn;
	long sym_offset;

	if (!image)
		return VM_FAULT_SIGBUS;

	sym_offset = (long)(vmf->pgoff << PAGE_SHIFT) + image->sym_vvar_start;

	if (sym_offset == 0)
		return VM_FAULT_SIGBUS;

	if (sym_offset == image->sym_vvar_page) {
		/* timens_page handling removed - always NULL */
		pfn = __pa_symbol(&__vvar_page) >> PAGE_SHIFT;
		return vmf_insert_pfn(vma, vmf->address, pfn);
	}
	/* pvclock, hvclock, timens branches removed:
	 * - pvclock_get_pvti_cpu0_va always returns NULL
	 * - hv_get_tsc_page always returns NULL
	 * - find_timens_vvar_page always returns NULL */

	return VM_FAULT_SIGBUS;
}

static const struct vm_special_mapping vdso_mapping = {
	.name = "[vdso]",
	.fault = vdso_fault,
	/* mremap removed - mremap syscall returns ENOSYS */
};
static const struct vm_special_mapping vvar_mapping = {
	.name = "[vvar]",
	.fault = vvar_fault,
};

static int map_vdso(const struct vdso_image *image, unsigned long addr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long text_start;
	int ret = 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	addr = get_unmapped_area(NULL, addr,
				 image->size - image->sym_vvar_start, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	text_start = addr - image->sym_vvar_start;

	vma = _install_special_mapping(mm, text_start, image->size,
				       VM_READ | VM_EXEC | VM_MAYREAD |
					       VM_MAYWRITE | VM_MAYEXEC,
				       &vdso_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	vma = _install_special_mapping(mm, addr, -image->sym_vvar_start,
				       VM_READ | VM_MAYREAD | VM_IO |
					       VM_DONTDUMP | VM_PFNMAP,
				       &vvar_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		do_munmap(mm, text_start, image->size, NULL);
	} else {
		current->mm->context.vdso = (void __user *)text_start;
		current->mm->context.vdso_image = image;
	}

up_fail:
	mmap_write_unlock(mm);
	return ret;
}

static int load_vdso32(void)
{
	if (vdso32_enabled != 1)
		return 0;

	return map_vdso(&vdso_image_32, 0);
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	return load_vdso32();
}

bool arch_syscall_is_vdso_sigreturn(struct pt_regs *regs)
{
	const struct vdso_image *image = current->mm->context.vdso_image;
	unsigned long vdso = (unsigned long)current->mm->context.vdso;

	if (in_ia32_syscall() && image == &vdso_image_32) {
		if (regs->ip ==
			    vdso + image->sym_vdso32_sigreturn_landing_pad ||
		    regs->ip ==
			    vdso + image->sym_vdso32_rt_sigreturn_landing_pad)
			return true;
	}
	return false;
}
