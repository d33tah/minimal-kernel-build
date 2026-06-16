#include <linux/mm.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/random.h>
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
		unsigned long old_land_addr = vdso_land +
			(unsigned long)current->mm->context.vdso;

		 
		if (regs->ip == old_land_addr)
			regs->ip = new_vma->vm_start + vdso_land;
	}
}

static int vdso_mremap(const struct vm_special_mapping *sm,
		struct vm_area_struct *new_vma)
{
	const struct vdso_image *image = current->mm->context.vdso_image;

	vdso_fix_landing(image, new_vma);
	current->mm->context.vdso = (void __user *)new_vma->vm_start;

	return 0;
}

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
		      struct vm_area_struct *vma, struct vm_fault *vmf)
{
	const struct vdso_image *image = vma->vm_mm->context.vdso_image;
	unsigned long pfn;
	long sym_offset;

	if (!image)
		return VM_FAULT_SIGBUS;

	sym_offset = (long)(vmf->pgoff << PAGE_SHIFT) +
		image->sym_vvar_start;

	 
	if (sym_offset == 0)
		return VM_FAULT_SIGBUS;

	if (sym_offset == image->sym_vvar_page) {
		pfn = __pa_symbol(&__vvar_page) >> PAGE_SHIFT;

		return vmf_insert_pfn(vma, vmf->address, pfn);
	} else if (sym_offset == image->sym_pvclock_page) {
		struct pvclock_vsyscall_time_info *pvti =
			pvclock_get_pvti_cpu0_va();
		if (pvti && vclock_was_used(VDSO_CLOCKMODE_PVCLOCK)) {
			return vmf_insert_pfn_prot(vma, vmf->address,
					__pa(pvti) >> PAGE_SHIFT,
					pgprot_decrypted(vma->vm_page_prot));
		}
	}

	return VM_FAULT_SIGBUS;
}

static const struct vm_special_mapping vdso_mapping = {
	.name = "[vdso]",
	.fault = vdso_fault,
	.mremap = vdso_mremap,
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

	 
	vma = _install_special_mapping(mm,
				       text_start,
				       image->size,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       &vdso_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	vma = _install_special_mapping(mm,
				       addr,
				       -image->sym_vvar_start,
				       VM_READ|VM_MAYREAD|VM_IO|VM_DONTDUMP|
				       VM_PFNMAP,
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

