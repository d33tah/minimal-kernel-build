
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/rbtree_augmented.h>
#include <linux/sched/mm.h>

#include <asm/tlb.h>
#include <asm/mmu_context.h>

#include "internal.h"

static unsigned long get_unmapped_area(struct file *, unsigned long,
				       unsigned long, unsigned long,
				       unsigned long);

/* mmap_min_addr moved from security/min_addr.c */
unsigned long mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;

pgprot_t protection_map[16] __ro_after_init = {
	[VM_NONE] = __P000,
	[VM_READ] = __P001,
	[VM_WRITE] = __P010,
	[VM_WRITE | VM_READ] = __P011,
	[VM_EXEC] = __P100,
	[VM_EXEC | VM_READ] = __P101,
	[VM_EXEC | VM_WRITE] = __P110,
	[VM_EXEC | VM_WRITE | VM_READ] = __P111,
	[VM_SHARED] = __S000,
	[VM_SHARED | VM_READ] = __S001,
	[VM_SHARED | VM_WRITE] = __S010,
	[VM_SHARED | VM_WRITE | VM_READ] = __S011,
	[VM_SHARED | VM_EXEC] = __S100,
	[VM_SHARED | VM_EXEC | VM_READ] = __S101,
	[VM_SHARED | VM_EXEC | VM_WRITE] = __S110,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ] = __S111
};

static inline unsigned long vma_compute_gap(struct vm_area_struct *vma)
{
	unsigned long gap, prev_end;

	gap = vm_start_gap(vma);
	if (vma->vm_prev) {
		prev_end = vm_end_gap(vma->vm_prev);
		if (gap > prev_end)
			gap -= prev_end;
		else
			gap = 0;
	}
	return gap;
}

RB_DECLARE_CALLBACKS_MAX(static, vma_gap_callbacks, struct vm_area_struct,
			 vm_rb, unsigned long, rb_subtree_gap, vma_compute_gap)

#define vma_gap_update(vma) vma_gap_callbacks_propagate(&(vma)->vm_rb, NULL)

static int find_vma_links(struct mm_struct *mm, unsigned long addr,
			  unsigned long end, struct vm_area_struct **pprev,
			  struct rb_node ***rb_link, struct rb_node **rb_parent)
{
	struct rb_node **__rb_link, *__rb_parent, *rb_prev;

	mmap_assert_locked(mm);
	__rb_link = &mm->mm_rb.rb_node;
	rb_prev = __rb_parent = NULL;

	while (*__rb_link) {
		struct vm_area_struct *vma_tmp;

		__rb_parent = *__rb_link;
		vma_tmp = rb_entry(__rb_parent, struct vm_area_struct, vm_rb);

		if (vma_tmp->vm_end > addr) {
			if (vma_tmp->vm_start < end)
				return -ENOMEM;
			__rb_link = &__rb_parent->rb_left;
		} else {
			rb_prev = __rb_parent;
			__rb_link = &__rb_parent->rb_right;
		}
	}

	*pprev = NULL;
	if (rb_prev)
		*pprev = rb_entry(rb_prev, struct vm_area_struct, vm_rb);
	*rb_link = __rb_link;
	*rb_parent = __rb_parent;
	return 0;
}

static void __vma_link_rb(struct mm_struct *mm, struct vm_area_struct *vma,
			  struct rb_node **rb_link, struct rb_node *rb_parent)
{
	if (vma->vm_next)
		vma_gap_update(vma->vm_next);
	else
		mm->highest_vm_end = vm_end_gap(vma);

	rb_link_node(&vma->vm_rb, rb_parent, rb_link);
	vma->rb_subtree_gap = 0;
	vma_gap_update(vma);
	rb_insert_augmented(&vma->vm_rb, &mm->mm_rb, &vma_gap_callbacks);
}

static void vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
		     struct vm_area_struct *prev, struct rb_node **rb_link,
		     struct rb_node *rb_parent)
{
	__vma_link_list(mm, vma, prev);
	__vma_link_rb(mm, vma, rb_link, rb_parent);
	mm->map_count++;
}

int __vma_adjust(struct vm_area_struct *vma, unsigned long start,
		 unsigned long end, pgoff_t pgoff,
		 struct vm_area_struct *insert, struct vm_area_struct *expand)
{
	/* Minimal stub: simple VMA adjustment without complex merging */
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
	return 0;
}

static unsigned long mmap_region(struct file *file, unsigned long addr,
				 unsigned long len, vm_flags_t vm_flags,
				 unsigned long pgoff);

unsigned long do_mmap(struct file *file, unsigned long addr, unsigned long len,
		      unsigned long prot, unsigned long flags,
		      unsigned long pgoff, unsigned long *populate,
		      struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	vm_flags_t vm_flags;
	if (populate)
		*populate = 0;

	if (!len)
		return -EINVAL;

	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		if (!(file && path_noexec(&file->f_path)))
			prot |= PROT_EXEC;

	if (flags & MAP_FIXED_NOREPLACE)
		flags |= MAP_FIXED;

	if (!(flags & MAP_FIXED)) {
		addr &= PAGE_MASK;
		if (((void *)addr != NULL) && (addr < mmap_min_addr))
			addr = PAGE_ALIGN(mmap_min_addr);
	}

	len = PAGE_ALIGN(len);
	if (!len)
		return -ENOMEM;

	if ((pgoff + (len >> PAGE_SHIFT)) < pgoff)
		return -EOVERFLOW;

	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;

	addr = get_unmapped_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr))
		return addr;

	if (flags & MAP_FIXED_NOREPLACE) {
		if (find_vma_intersection(mm, addr, addr + len))
			return -EEXIST;
	}

	vm_flags = calc_vm_prot_bits(prot, 0) | calc_vm_flag_bits(flags) |
		   mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	if (file) {
		if (!(file->f_mode & FMODE_READ))
			return -EACCES;
		if (path_noexec(&file->f_path)) {
			if (vm_flags & VM_EXEC)
				return -EPERM;
			vm_flags &= ~VM_MAYEXEC;
		}
		if (!file->f_op->mmap)
			return -ENODEV;
	} else {
		pgoff = addr >> PAGE_SHIFT;
	}

	return mmap_region(file, addr, len, vm_flags, pgoff);
}

static unsigned long mmap_region(struct file *file, unsigned long addr,
				 unsigned long len, vm_flags_t vm_flags,
				 unsigned long pgoff)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct rb_node **rb_link, *rb_parent;
	struct vm_area_struct *prev = NULL;

	if (find_vma_links(mm, addr, addr + len, &prev, &rb_link, &rb_parent))
		return -ENOMEM;

	vma = vm_area_alloc(mm);
	if (!vma)
		return -ENOMEM;

	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = vm_flags;
	vma->vm_page_prot = vm_get_page_prot(vm_flags);
	vma->vm_pgoff = pgoff;

	if (file) {
		vma->vm_file = get_file(file);
		if (file->f_op->mmap(file, vma)) {
			fput(vma->vm_file);
			vm_area_free(vma);
			return -EINVAL;
		}
	} else {
		vma_set_anonymous(vma);
	}

	vma_link(mm, vma, prev, rb_link, rb_parent);
	return addr;
}

static unsigned long vm_unmapped_area(struct vm_unmapped_area_info *info)
{
	struct mm_struct *mm = current->mm;
	unsigned long gap_start;

	if (info->high_limit < info->length)
		return -ENOMEM;
	if (info->low_limit > info->high_limit - info->length)
		return -ENOMEM;

	gap_start = max(mm->highest_vm_end, info->low_limit);
	gap_start += (info->align_offset - gap_start) & info->align_mask;
	if (gap_start + info->length > info->high_limit)
		return -ENOMEM;
	return gap_start;
}

static unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr, unsigned long len,
		       unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_unmapped_area_info info;
	const unsigned long mmap_end = arch_get_mmap_end(addr, len, flags);

	if (len > mmap_end - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = mmap_end;
	info.align_mask = 0;
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

static unsigned long get_unmapped_area(struct file *file, unsigned long addr,
				       unsigned long len, unsigned long pgoff,
				       unsigned long flags)
{
	if (len > TASK_SIZE)
		return -ENOMEM;

	addr = arch_get_unmapped_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr))
		return addr;

	if (addr > TASK_SIZE - len)
		return -ENOMEM;
	if (offset_in_page(addr))
		return -EINVAL;

	return addr;
}

struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	struct rb_node *rb_node;
	struct vm_area_struct *vma;

	mmap_assert_locked(mm);

	rb_node = mm->mm_rb.rb_node;

	while (rb_node) {
		struct vm_area_struct *tmp;

		tmp = rb_entry(rb_node, struct vm_area_struct, vm_rb);

		if (tmp->vm_end > addr) {
			vma = tmp;
			if (tmp->vm_start <= addr)
				break;
			rb_node = rb_node->rb_left;
		} else
			rb_node = rb_node->rb_right;
	}

	return vma;
}

unsigned long stack_guard_gap = 256UL << PAGE_SHIFT;

int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;

	address &= PAGE_MASK;
	if (address < vma->vm_start) {
		unsigned long grow = (vma->vm_start - address) >> PAGE_SHIFT;

		if (unlikely(anon_vma_prepare(vma)))
			return -ENOMEM;
		spin_lock(&mm->page_table_lock);
		vma->vm_start = address;
		vma->vm_pgoff -= grow;
		vma_gap_update(vma);
		spin_unlock(&mm->page_table_lock);
	}
	return 0;
}

struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;

	addr &= PAGE_MASK;
	vma = find_vma(mm, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (expand_stack(vma, addr))
		return NULL;
	return vma;
}

int vm_brk_flags(unsigned long addr, unsigned long request, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct rb_node **rb_link, *rb_parent;
	pgoff_t pgoff;
	unsigned long len;
	int ret = 0;

	len = PAGE_ALIGN(request);
	if (!len)
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	pgoff = addr >> PAGE_SHIFT;
	flags |= VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT | mm->def_flags;

	if (find_vma_links(mm, addr, addr + len, &prev, &rb_link, &rb_parent)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	vma = vm_area_alloc(mm);
	if (!vma) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	vma_set_anonymous(vma);
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_pgoff = pgoff;
	vma->vm_flags = flags;
	vma->vm_page_prot = vm_get_page_prot(flags);
	vma_link(mm, vma, prev, rb_link, rb_parent);

out_unlock:
	mmap_write_unlock(mm);
	return ret;
}

void exit_mmap(struct mm_struct *mm)
{
	/* Hello World process never exits - stub */
}

int insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *prev;
	struct rb_node **rb_link, *rb_parent;

	if (find_vma_links(mm, vma->vm_start, vma->vm_end, &prev, &rb_link,
			   &rb_parent))
		return -ENOMEM;

	if (vma_is_anonymous(vma)) {
		vma->vm_pgoff = vma->vm_start >> PAGE_SHIFT;
	}

	vma_link(mm, vma, prev, rb_link, rb_parent);
	return 0;
}

void __init mmap_init(void)
{
	percpu_counter_init(&vm_committed_as, 0, GFP_KERNEL);
}
