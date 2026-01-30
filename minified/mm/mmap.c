
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/vmacache.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/shmem_fs.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/mmdebug.h>
#include <linux/rbtree_augmented.h>
#include <linux/printk.h>
/* userfaultfd_k.h removed - empty stubs */
#include <linux/sched/mm.h>

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>

#include "internal.h"

/* mmap_min_addr moved from security/min_addr.c */
unsigned long mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;

#ifndef arch_mmap_check
#define arch_mmap_check(addr, len, flags) (0)
#endif

/* mmap_rnd_bits removed - no ASLR */

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

/* Removed: vm_pgprot_modify, vma_set_page_prot - never called */

void unlink_file_vma(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;

	if (file) {
		struct address_space *mapping = file->f_mapping;
		i_mmap_lock_write(mapping);
		if (vma->vm_flags & VM_SHARED)
			atomic_dec(
				&mapping->i_mmap_writable); /* inlined mapping_unmap_writable */
		vma_interval_tree_remove(vma, &mapping->i_mmap);
		i_mmap_unlock_write(mapping);
	}
}

static struct vm_area_struct *remove_vma(struct vm_area_struct *vma)
{
	struct vm_area_struct *next = vma->vm_next;
	if (vma->vm_file)
		fput(vma->vm_file);
	vm_area_free(vma);
	return next;
}

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

/* validate_mm_rb macro removed - never called */
#define validate_mm(mm) \
	do {            \
	} while (0)

RB_DECLARE_CALLBACKS_MAX(static, vma_gap_callbacks, struct vm_area_struct,
			 vm_rb, unsigned long, rb_subtree_gap, vma_compute_gap)

/* vma_gap_update removed - converted to macro for 4 callers (~4 LOC) */
#define vma_gap_update(vma) vma_gap_callbacks_propagate(&(vma)->vm_rb, NULL)

/* anon_vma_interval_tree_pre/post_update_vma inlined into expand_stack (~16 LOC) */

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

static inline struct vm_area_struct *vma_next(struct mm_struct *mm,
					      struct vm_area_struct *vma)
{
	if (!vma)
		return mm->mmap;

	return vma->vm_next;
}

static inline int
munmap_vma_range(struct mm_struct *mm, unsigned long start, unsigned long len,
		 struct vm_area_struct **pprev, struct rb_node ***link,
		 struct rb_node **parent, struct list_head *uf)
{
	while (find_vma_links(mm, start, start + len, pprev, link, parent))
		if (do_munmap(mm, start, len, uf))
			return -ENOMEM;

	return 0;
}

void __vma_link_rb(struct mm_struct *mm, struct vm_area_struct *vma,
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
	struct address_space *mapping = NULL;
	struct file *file;

	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;
		i_mmap_lock_write(mapping);
	}

	__vma_link_list(mm, vma, prev);
	__vma_link_rb(mm, vma, rb_link, rb_parent);

	file = vma->vm_file;
	if (file) {
		struct address_space *file_mapping = file->f_mapping;

		if (vma->vm_flags & VM_SHARED)
			atomic_inc(
				&file_mapping
					 ->i_mmap_writable); /* inlined mapping_allow_writable */

		vma_interval_tree_insert(vma, &file_mapping->i_mmap);
	}

	if (mapping)
		i_mmap_unlock_write(mapping);

	mm->map_count++;
	validate_mm(mm);
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

/* VMA merging disabled - always return NULL/allocate new VMAs */
/* Removed: is_mergeable_vma, is_mergeable_anon_vma, can_vma_merge_before */
/* Removed: reusable_anon_vma - only used by find_mergeable_anon_vma */

struct vm_area_struct *
vma_merge(struct mm_struct *mm, struct vm_area_struct *prev, unsigned long addr,
	  unsigned long end, unsigned long vm_flags, struct anon_vma *anon_vma,
	  struct file *file, pgoff_t pgoff, struct mempolicy *policy,
	  struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
	  struct anon_vma_name *anon_name)
{
	/* VMA merging disabled - always allocate new VMAs */
	return NULL;
}

struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *vma)
{
	/* VMA merging disabled - always allocate new anon_vma */
	return NULL;
}

/* mlock_future_check removed - always returned 0 */
/* file_mmap_ok inlined into do_mmap - only called once */

unsigned long do_mmap(struct file *file, unsigned long addr, unsigned long len,
		      unsigned long prot, unsigned long flags,
		      unsigned long pgoff, unsigned long *populate,
		      struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	vm_flags_t vm_flags;
	int pkey = 0;

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

	if (prot == PROT_EXEC) {
		pkey = execute_only_pkey(mm);
		if (pkey < 0)
			pkey = 0;
	}

	vm_flags = calc_vm_prot_bits(prot, pkey) | calc_vm_flag_bits(flags) |
		   mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	if (flags & MAP_LOCKED)
		return -EPERM;

	if (file) {
		struct inode *inode = file_inode(file);
		unsigned long flags_mask;
		u64 maxsize;

		/* Inlined file_mmap_ok */
		if (S_ISREG(inode->i_mode) || S_ISBLK(inode->i_mode) ||
		    S_ISSOCK(inode->i_mode))
			maxsize = MAX_LFS_FILESIZE;
		else if (file->f_mode & FMODE_UNSIGNED_OFFSET)
			maxsize = 0;
		else
			maxsize = ULONG_MAX;

		if (maxsize && len > maxsize)
			return -EOVERFLOW;
		maxsize -= len;
		if (pgoff > maxsize >> PAGE_SHIFT)
			return -EOVERFLOW;

		flags_mask = LEGACY_MAP_MASK | file->f_op->mmap_supported_flags;

		switch (flags & MAP_TYPE) {
		case MAP_SHARED:

			flags &= LEGACY_MAP_MASK;
			fallthrough;
		case MAP_SHARED_VALIDATE:
			if (flags & ~flags_mask)
				return -EOPNOTSUPP;
			if (prot & PROT_WRITE) {
				if (!(file->f_mode & FMODE_WRITE))
					return -EACCES;
				if (IS_SWAPFILE(file->f_mapping->host))
					return -ETXTBSY;
			}

			if (IS_APPEND(inode) && (file->f_mode & FMODE_WRITE))
				return -EACCES;

			vm_flags |= VM_SHARED | VM_MAYSHARE;
			if (!(file->f_mode & FMODE_WRITE))
				vm_flags &= ~(VM_MAYWRITE | VM_SHARED);
			fallthrough;
		case MAP_PRIVATE:
			if (!(file->f_mode & FMODE_READ))
				return -EACCES;
			if (path_noexec(&file->f_path)) {
				if (vm_flags & VM_EXEC)
					return -EPERM;
				vm_flags &= ~VM_MAYEXEC;
			}

			if (!file->f_op->mmap)
				return -ENODEV;
			if (vm_flags & (VM_GROWSDOWN | VM_GROWSUP))
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}
	} else {
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if (vm_flags & (VM_GROWSDOWN | VM_GROWSUP))
				return -EINVAL;

			pgoff = 0;
			vm_flags |= VM_SHARED | VM_MAYSHARE;
			break;
		case MAP_PRIVATE:

			pgoff = addr >> PAGE_SHIFT;
			break;
		default:
			return -EINVAL;
		}
	}

	if (flags & MAP_NORESERVE) {
		if (sysctl_overcommit_memory != OVERCOMMIT_NEVER)
			vm_flags |= VM_NORESERVE;
		/* is_file_hugepages always returns false - hugetlb disabled */
	}

	addr = mmap_region(file, addr, len, vm_flags, pgoff, uf);
	if (!IS_ERR_VALUE(addr) &&
	    ((vm_flags & VM_LOCKED) ||
	     (flags & (MAP_POPULATE | MAP_NONBLOCK)) == MAP_POPULATE))
		*populate = len;
	return addr;
}

/* Removed: vma_wants_writenotify - was used only by vma_set_page_prot (~4 LOC) */

unsigned long mmap_region(struct file *file, unsigned long addr,
			  unsigned long len, vm_flags_t vm_flags,
			  unsigned long pgoff, struct list_head *uf)
{
	/* Minimal stub: simplified mmap without complex VMA merging/splitting */
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct rb_node **rb_link, *rb_parent;
	struct vm_area_struct *prev = NULL;

	if (munmap_vma_range(mm, addr, len, &prev, &rb_link, &rb_parent, uf))
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
		/* call_mmap inlined */
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

unsigned long vm_unmapped_area(struct vm_unmapped_area_info *info)
{
	struct mm_struct *mm = current->mm;

	/* Basic bounds checking */
	if (info->high_limit < info->length)
		return -ENOMEM;
	if (info->low_limit > info->high_limit - info->length)
		return -ENOMEM;

	if (info->flags & VM_UNMAPPED_AREA_TOPDOWN) {
		/* Inlined unmapped_area_topdown */
		unsigned long gap_end = info->high_limit;
		if (mm->highest_vm_end <= info->high_limit)
			gap_end = mm->highest_vm_end;
		gap_end -= info->length;
		gap_end -= (gap_end - info->align_offset) & info->align_mask;
		if (gap_end < info->low_limit)
			return -ENOMEM;
		return gap_end;
	} else {
		/* Inlined unmapped_area */
		unsigned long gap_start =
			max(mm->highest_vm_end, info->low_limit);
		gap_start += (info->align_offset - gap_start) &
			     info->align_mask;
		if (gap_start + info->length > info->high_limit)
			return -ENOMEM;
		return gap_start;
	}
}

unsigned long generic_get_unmapped_area(struct file *filp, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct vm_unmapped_area_info info;
	const unsigned long mmap_end = arch_get_mmap_end(addr, len, flags);

	if (len > mmap_end - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma_prev(mm, addr, &prev);
		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)) &&
		    (!prev || addr >= vm_end_gap(prev)))
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = mmap_end;
	info.align_mask = 0;
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

#ifndef HAVE_ARCH_UNMAPPED_AREA
unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
				     unsigned long len, unsigned long pgoff,
				     unsigned long flags)
{
	return generic_get_unmapped_area(filp, addr, len, pgoff, flags);
}
#endif

unsigned long generic_get_unmapped_area_topdown(struct file *filp,
						unsigned long addr,
						unsigned long len,
						unsigned long pgoff,
						unsigned long flags)
{
	struct vm_area_struct *vma, *prev;
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info;
	const unsigned long mmap_end = arch_get_mmap_end(addr, len, flags);

	if (len > mmap_end - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma_prev(mm, addr, &prev);
		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)) &&
		    (!prev || addr >= vm_end_gap(prev)))
			return addr;
	}

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = max(PAGE_SIZE, mmap_min_addr);
	info.high_limit = arch_get_mmap_base(addr, mm->mmap_base);
	info.align_mask = 0;
	info.align_offset = 0;
	addr = vm_unmapped_area(&info);

	if (offset_in_page(addr)) {
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = mmap_end;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

#ifndef HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
unsigned long arch_get_unmapped_area_topdown(struct file *filp,
					     unsigned long addr,
					     unsigned long len,
					     unsigned long pgoff,
					     unsigned long flags)
{
	return generic_get_unmapped_area_topdown(filp, addr, len, pgoff, flags);
}
#endif

unsigned long get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags)
{
	unsigned long (*get_area)(struct file *, unsigned long, unsigned long,
				  unsigned long, unsigned long);

	unsigned long error = arch_mmap_check(addr, len, flags);
	if (error)
		return error;

	if (len > TASK_SIZE)
		return -ENOMEM;

	get_area = current->mm->get_unmapped_area;
	if (file) {
		if (file->f_op->get_unmapped_area)
			get_area = file->f_op->get_unmapped_area;
	} else if (flags & MAP_SHARED) {
		pgoff = 0;
		get_area = shmem_get_unmapped_area;
	}

	addr = get_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr))
		return addr;

	if (addr > TASK_SIZE - len)
		return -ENOMEM;
	if (offset_in_page(addr))
		return -EINVAL;

	/* security_mmap_addr always returns 0 */
	return addr;
}

struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	struct rb_node *rb_node;
	struct vm_area_struct *vma;

	mmap_assert_locked(mm);

	vma = vmacache_find(mm, addr);
	if (likely(vma))
		return vma;

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

	if (vma)
		vmacache_update(addr, vma);
	return vma;
}

/* Used by generic_get_unmapped_area and generic_get_unmapped_area_topdown */
struct vm_area_struct *find_vma_prev(struct mm_struct *mm, unsigned long addr,
				     struct vm_area_struct **pprev)
{
	*pprev = NULL;
	return find_vma(mm, addr);
}

int expand_downwards(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *prev;
	int error = 0;

	address &= PAGE_MASK;
	if (address < mmap_min_addr)
		return -EPERM;

	prev = vma->vm_prev;

	if (prev && !(prev->vm_flags & VM_GROWSDOWN) &&
	    vma_is_accessible(prev)) {
		if (address - prev->vm_end < stack_guard_gap)
			return -ENOMEM;
	}

	if (unlikely(anon_vma_prepare(vma)))
		return -ENOMEM;

	anon_vma_lock_write(vma->anon_vma);

	if (address < vma->vm_start) {
		unsigned long size, grow;

		size = vma->vm_end - address;
		grow = (vma->vm_start - address) >> PAGE_SHIFT;

		error = -ENOMEM;
		if (grow <= vma->vm_pgoff && size <= rlimit(RLIMIT_STACK) &&
		    !security_vm_enough_memory_mm(mm, grow)) {
			error = 0;
			spin_lock(&mm->page_table_lock);
			/* inlined anon_vma_interval_tree_pre_update_vma */
			{
				struct anon_vma_chain *avc;
				list_for_each_entry(avc, &vma->anon_vma_chain,
						    same_vma)
					anon_vma_interval_tree_remove(
						avc, &avc->anon_vma->rb_root);
			}
			vma->vm_start = address;
			vma->vm_pgoff -= grow;
			/* inlined anon_vma_interval_tree_post_update_vma */
			{
				struct anon_vma_chain *avc;
				list_for_each_entry(avc, &vma->anon_vma_chain,
						    same_vma)
					anon_vma_interval_tree_insert(
						avc, &avc->anon_vma->rb_root);
			}
			vma_gap_update(vma);
			spin_unlock(&mm->page_table_lock);
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	validate_mm(mm);
	return error;
}

unsigned long stack_guard_gap = 256UL << PAGE_SHIFT;

int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	return expand_downwards(vma, address);
}

struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;
	unsigned long start;

	addr &= PAGE_MASK;
	vma = find_vma(mm, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	start = vma->vm_start;
	if (expand_stack(vma, addr))
		return NULL;
	if (vma->vm_flags & VM_LOCKED)
		populate_vma_page_range(vma, addr, start, NULL);
	return vma;
}

/* Removed: unmap_region - inlined into __do_munmap */
/* Removed: detach_vmas_to_be_unmapped - inlined into __do_munmap */

int __split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, int new_below)
{
	struct vm_area_struct *new;
	int err;

	if (vma->vm_ops && vma->vm_ops->may_split) {
		err = vma->vm_ops->may_split(vma, addr);
		if (err)
			return err;
	}

	new = vm_area_dup(vma);
	if (!new)
		return -ENOMEM;

	if (new_below)
		new->vm_end = addr;
	else {
		new->vm_start = addr;
		new->vm_pgoff += ((addr - vma->vm_start) >> PAGE_SHIFT);
	}

	err = anon_vma_clone(new, vma);
	if (err)
		goto out_free_vma;

	if (new->vm_file)
		get_file(new->vm_file);

	/* vm_ops->open call removed - never assigned */

	if (new_below)
		err = vma_adjust(vma, addr, vma->vm_end,
				 vma->vm_pgoff +
					 ((addr - new->vm_start) >> PAGE_SHIFT),
				 new);
	else
		err = vma_adjust(vma, vma->vm_start, addr, vma->vm_pgoff, new);

	if (!err)
		return 0;

	/* vm_ops->close call removed - never assigned */
	if (new->vm_file)
		fput(new->vm_file);
	unlink_anon_vmas(new);
out_free_vma:
	vm_area_free(new);
	return err;
}

int __do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
		struct list_head *uf, bool downgrade)
{
	unsigned long end;
	struct vm_area_struct *vma, *prev, *last;

	if ((offset_in_page(start)) || start > TASK_SIZE ||
	    len > TASK_SIZE - start)
		return -EINVAL;

	len = PAGE_ALIGN(len);
	end = start + len;
	if (len == 0)
		return -EINVAL;

	arch_unmap(mm, start, end);

	vma = find_vma_intersection(mm, start, end);
	if (!vma)
		return 0;
	prev = vma->vm_prev;

	if (start > vma->vm_start) {
		int error;

		if (end < vma->vm_end && mm->map_count >= sysctl_max_map_count)
			return -ENOMEM;

		error = __split_vma(mm, vma, start, 0);
		if (error)
			return error;
		prev = vma;
	}

	last = find_vma(mm, end);
	if (last && end > last->vm_start) {
		int error = __split_vma(mm, last, end, 1);
		if (error)
			return error;
	}
	vma = vma_next(mm, prev);

	/* userfaultfd_unmap_prep always returns 0 - dead code removed */
	/* detach_vmas_to_be_unmapped inlined */
	{
		struct vm_area_struct **insertion_point;
		struct vm_area_struct *tail_vma = NULL;
		struct vm_area_struct *tmp_vma = vma;
		insertion_point = (prev ? &prev->vm_next : &mm->mmap);
		vma->vm_prev = NULL;
		do {
			rb_erase_augmented(&tmp_vma->vm_rb, &mm->mm_rb,
					   &vma_gap_callbacks);
			mm->map_count--;
			tail_vma = tmp_vma;
			tmp_vma = tmp_vma->vm_next;
		} while (tmp_vma && tmp_vma->vm_start < end);
		*insertion_point = tmp_vma;
		if (tmp_vma) {
			tmp_vma->vm_prev = prev;
			vma_gap_update(tmp_vma);
		} else
			mm->highest_vm_end = prev ? vm_end_gap(prev) : 0;
		tail_vma->vm_next = NULL;
		mm->vmacache_seqnum++;
		if (tmp_vma && (tmp_vma->vm_flags & VM_GROWSDOWN))
			downgrade = false;
		if (prev && (prev->vm_flags & VM_GROWSUP))
			downgrade = false;
	}

	if (downgrade)
		mmap_write_downgrade(mm);

	/* unmap_region inlined */
	{
		struct vm_area_struct *next = vma_next(mm, prev);
		struct mmu_gather tlb;
		lru_add_drain();
		tlb_gather_mmu(&tlb, mm);
		unmap_vmas(&tlb, vma, start, end);
		free_pgtables(&tlb, vma,
			      prev ? prev->vm_end : FIRST_USER_ADDRESS,
			      next ? next->vm_start : USER_PGTABLES_CEILING);
		tlb_finish_mmu(&tlb);
	}

	/* Inlined remove_vma_list */
	{
		unsigned long nr_accounted = 0;
		struct vm_area_struct *tmp_vma = vma;
		do {
			long nrpages = vma_pages(tmp_vma);
			if (tmp_vma->vm_flags & VM_ACCOUNT)
				nr_accounted += nrpages;
			tmp_vma = remove_vma(tmp_vma);
		} while (tmp_vma);
		vm_unacct_memory(nr_accounted);
		validate_mm(mm);
	}

	return downgrade ? 1 : 0;
}

int do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
	      struct list_head *uf)
{
	return __do_munmap(mm, start, len, uf, false);
}

int vm_munmap(unsigned long start, size_t len)
{
	int ret;
	struct mm_struct *mm = current->mm;
	LIST_HEAD(uf);

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = __do_munmap(mm, start, len, &uf, false);
	mmap_write_unlock(mm);
	/* userfaultfd_unmap_complete call removed - empty stub */
	return ret;
}

/* remap_file_pages removed - COND_SYSCALL provides stub */
/* do_brk_flags inlined into vm_brk_flags */

int vm_brk_flags(unsigned long addr, unsigned long request, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct rb_node **rb_link, *rb_parent;
	pgoff_t pgoff;
	unsigned long len, mapped_addr;
	int ret = 0;
	bool populate;
	LIST_HEAD(uf);

	len = PAGE_ALIGN(request);
	if (len < request)
		return -ENOMEM;
	if (!len)
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	/* Inlined do_brk_flags */
	pgoff = addr >> PAGE_SHIFT;

	if ((flags & (~VM_EXEC)) != 0) {
		ret = -EINVAL;
		goto out_unlock;
	}
	flags |= VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT | mm->def_flags;

	mapped_addr = get_unmapped_area(NULL, addr, len, 0, MAP_FIXED);
	if (IS_ERR_VALUE(mapped_addr)) {
		ret = mapped_addr;
		goto out_unlock;
	}

	if (munmap_vma_range(mm, addr, len, &prev, &rb_link, &rb_parent, &uf)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	if (mm->map_count > sysctl_max_map_count) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	if (security_vm_enough_memory_mm(mm, len >> PAGE_SHIFT)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	vma = vma_merge(mm, prev, addr, addr + len, flags, NULL, NULL, pgoff,
			NULL, NULL_VM_UFFD_CTX, NULL);
	if (!vma) {
		vma = vm_area_alloc(mm);
		if (!vma) {
			vm_unacct_memory(len >> PAGE_SHIFT);
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
	}
	mm->total_vm += len >> PAGE_SHIFT;
	vma->vm_flags |= VM_SOFTDIRTY;

out_unlock:
	populate = ((mm->def_flags & VM_LOCKED) != 0);
	mmap_write_unlock(mm);
	if (populate && !ret)
		mm_populate(addr, len);
	return ret;
}

void exit_mmap(struct mm_struct *mm)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma;
	unsigned long nr_accounted = 0;

	/* mmu_notifier_release, mm_is_oom_victim() removed - OOM killer disabled, empty stub */
	mmap_write_lock(mm);
	arch_exit_mmap(mm);

	vma = mm->mmap;
	if (!vma) {
		mmap_write_unlock(mm);
		return;
	}

	lru_add_drain();
	/* flush_cache_mm - empty stub on x86 */
	tlb_gather_mmu_fullmm(&tlb, mm);

	unmap_vmas(&tlb, vma, 0, -1);
	free_pgtables(&tlb, vma, FIRST_USER_ADDRESS, USER_PGTABLES_CEILING);
	tlb_finish_mmu(&tlb);

	while (vma) {
		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += vma_pages(vma);
		vma = remove_vma(vma);
		cond_resched();
	}
	mm->mmap = NULL;
	mmap_write_unlock(mm);
	vm_unacct_memory(nr_accounted);
}

int insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *prev;
	struct rb_node **rb_link, *rb_parent;

	if (find_vma_links(mm, vma->vm_start, vma->vm_end, &prev, &rb_link,
			   &rb_parent))
		return -ENOMEM;
	if ((vma->vm_flags & VM_ACCOUNT) &&
	    security_vm_enough_memory_mm(mm, vma_pages(vma)))
		return -ENOMEM;

	if (vma_is_anonymous(vma)) {
		BUG_ON(vma->anon_vma);
		vma->vm_pgoff = vma->vm_start >> PAGE_SHIFT;
	}

	vma_link(mm, vma, prev, rb_link, rb_parent);
	return 0;
}

static vm_fault_t special_mapping_fault(struct vm_fault *vmf);

/* special_mapping_mremap removed - mremap syscall returns ENOSYS */

static int special_mapping_split(struct vm_area_struct *vma, unsigned long addr)
{
	return -EINVAL;
}

static const struct vm_operations_struct special_mapping_vmops = {
	.fault = special_mapping_fault,
	/* mremap removed - mremap syscall returns ENOSYS */
	.may_split = special_mapping_split,
};

static const struct vm_operations_struct legacy_special_mapping_vmops = {
	.fault = special_mapping_fault,
};

static vm_fault_t special_mapping_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	pgoff_t pgoff;
	struct page **pages;

	if (vma->vm_ops == &legacy_special_mapping_vmops) {
		pages = vma->vm_private_data;
	} else {
		struct vm_special_mapping *sm = vma->vm_private_data;

		if (sm->fault)
			return sm->fault(sm, vmf->vma, vmf);

		pages = sm->pages;
	}

	for (pgoff = vmf->pgoff; pgoff && *pages; ++pages)
		pgoff--;

	if (*pages) {
		struct page *page = *pages;
		get_page(page);
		vmf->page = page;
		return 0;
	}

	return VM_FAULT_SIGBUS;
}

/* __install_special_mapping inlined into _install_special_mapping */

struct vm_area_struct *
_install_special_mapping(struct mm_struct *mm, unsigned long addr,
			 unsigned long len, unsigned long vm_flags,
			 const struct vm_special_mapping *spec)
{
	int ret;
	struct vm_area_struct *vma;

	vma = vm_area_alloc(mm);
	if (unlikely(vma == NULL))
		return ERR_PTR(-ENOMEM);

	vma->vm_start = addr;
	vma->vm_end = addr + len;

	vma->vm_flags = vm_flags | mm->def_flags | VM_DONTEXPAND | VM_SOFTDIRTY;
	vma->vm_flags &= VM_LOCKED_CLEAR_MASK;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	vma->vm_ops = &special_mapping_vmops;
	vma->vm_private_data = (void *)spec;

	ret = insert_vm_struct(mm, vma);
	if (ret) {
		vm_area_free(vma);
		return ERR_PTR(ret);
	}

	return vma;
}

void __init mmap_init(void)
{
	percpu_counter_init(&vm_committed_as, 0, GFP_KERNEL);
}
