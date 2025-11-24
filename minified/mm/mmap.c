 

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/vmacache.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/shmem_fs.h>
#include <linux/export.h>
#include <linux/mount.h>
#include <linux/mempolicy.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/mmdebug.h>
#include <linux/perf_event.h>
#include <linux/audit.h>
#include <linux/khugepaged.h>
#include <linux/uprobes.h>
#include <linux/rbtree_augmented.h>
#include <linux/notifier.h>
#include <linux/memory.h>
#include <linux/printk.h>
#include <linux/userfaultfd_k.h>
#include <linux/moduleparam.h>
#include <linux/pkeys.h>
#include <linux/oom.h>
#include <linux/sched/mm.h>

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>

#include "internal.h"

#ifndef arch_mmap_check
#define arch_mmap_check(addr, len, flags)	(0)
#endif

const int mmap_rnd_bits_min = CONFIG_ARCH_MMAP_RND_BITS_MIN;
const int mmap_rnd_bits_max = CONFIG_ARCH_MMAP_RND_BITS_MAX;
int mmap_rnd_bits __read_mostly = CONFIG_ARCH_MMAP_RND_BITS;

static bool ignore_rlimit_data;
core_param(ignore_rlimit_data, ignore_rlimit_data, bool, 0644);

static void unmap_region(struct mm_struct *mm,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		unsigned long start, unsigned long end);

pgprot_t protection_map[16] __ro_after_init = {
	[VM_NONE]					= __P000,
	[VM_READ]					= __P001,
	[VM_WRITE]					= __P010,
	[VM_WRITE | VM_READ]				= __P011,
	[VM_EXEC]					= __P100,
	[VM_EXEC | VM_READ]				= __P101,
	[VM_EXEC | VM_WRITE]				= __P110,
	[VM_EXEC | VM_WRITE | VM_READ]			= __P111,
	[VM_SHARED]					= __S000,
	[VM_SHARED | VM_READ]				= __S001,
	[VM_SHARED | VM_WRITE]				= __S010,
	[VM_SHARED | VM_WRITE | VM_READ]		= __S011,
	[VM_SHARED | VM_EXEC]				= __S100,
	[VM_SHARED | VM_EXEC | VM_READ]			= __S101,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= __S110,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= __S111
};

static pgprot_t vm_pgprot_modify(pgprot_t oldprot, unsigned long vm_flags)
{
	return pgprot_modify(oldprot, vm_get_page_prot(vm_flags));
}

void vma_set_page_prot(struct vm_area_struct *vma)
{
	unsigned long vm_flags = vma->vm_flags;
	pgprot_t vm_page_prot;

	vm_page_prot = vm_pgprot_modify(vma->vm_page_prot, vm_flags);
	if (vma_wants_writenotify(vma, vm_page_prot)) {
		vm_flags &= ~VM_SHARED;
		vm_page_prot = vm_pgprot_modify(vm_page_prot, vm_flags);
	}
	
	WRITE_ONCE(vma->vm_page_prot, vm_page_prot);
}

static void __remove_shared_vm_struct(struct vm_area_struct *vma,
		struct file *file, struct address_space *mapping)
{
	if (vma->vm_flags & VM_SHARED)
		mapping_unmap_writable(mapping);

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_remove(vma, &mapping->i_mmap);
	flush_dcache_mmap_unlock(mapping);
}

void unlink_file_vma(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;

	if (file) {
		struct address_space *mapping = file->f_mapping;
		i_mmap_lock_write(mapping);
		__remove_shared_vm_struct(vma, file, mapping);
		i_mmap_unlock_write(mapping);
	}
}

static struct vm_area_struct *remove_vma(struct vm_area_struct *vma)
{
	struct vm_area_struct *next = vma->vm_next;

	might_sleep();
	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);
	if (vma->vm_file)
		fput(vma->vm_file);
	mpol_put(vma_policy(vma));
	vm_area_free(vma);
	return next;
}

static int do_brk_flags(unsigned long addr, unsigned long request, unsigned long flags,
		struct list_head *uf);
SYSCALL_DEFINE1(brk, unsigned long, brk)
{
	unsigned long newbrk, oldbrk, origbrk;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *next;
	unsigned long min_brk;
	bool populate;
	bool downgraded = false;
	LIST_HEAD(uf);

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	origbrk = mm->brk;

	min_brk = mm->start_brk;
	if (brk < min_brk)
		goto out;

	
	if (check_data_rlimit(rlimit(RLIMIT_DATA), brk, mm->start_brk,
			      mm->end_data, mm->start_data))
		goto out;

	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	if (oldbrk == newbrk) {
		mm->brk = brk;
		goto success;
	}

	
	if (brk <= mm->brk) {
		int ret;

		
		mm->brk = brk;
		ret = __do_munmap(mm, newbrk, oldbrk-newbrk, &uf, true);
		if (ret < 0) {
			mm->brk = origbrk;
			goto out;
		} else if (ret == 1) {
			downgraded = true;
		}
		goto success;
	}

	
	next = find_vma(mm, oldbrk);
	if (next && newbrk + PAGE_SIZE > vm_start_gap(next))
		goto out;

	
	if (do_brk_flags(oldbrk, newbrk-oldbrk, 0, &uf) < 0)
		goto out;
	mm->brk = brk;

success:
	populate = newbrk > oldbrk && (mm->def_flags & VM_LOCKED) != 0;
	if (downgraded)
		mmap_read_unlock(mm);
	else
		mmap_write_unlock(mm);
	userfaultfd_unmap_complete(mm, &uf);
	if (populate)
		mm_populate(oldbrk, newbrk - oldbrk);
	return brk;

out:
	mmap_write_unlock(mm);
	return origbrk;
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

#define validate_mm_rb(root, ignore) do { } while (0)
#define validate_mm(mm) do { } while (0)

RB_DECLARE_CALLBACKS_MAX(static, vma_gap_callbacks,
			 struct vm_area_struct, vm_rb,
			 unsigned long, rb_subtree_gap, vma_compute_gap)

static void vma_gap_update(struct vm_area_struct *vma)
{
	
	vma_gap_callbacks_propagate(&vma->vm_rb, NULL);
}

static inline void vma_rb_insert(struct vm_area_struct *vma,
				 struct rb_root *root)
{
	
	validate_mm_rb(root, NULL);

	rb_insert_augmented(&vma->vm_rb, root, &vma_gap_callbacks);
}

static void __vma_rb_erase(struct vm_area_struct *vma, struct rb_root *root)
{
	
	rb_erase_augmented(&vma->vm_rb, root, &vma_gap_callbacks);
}

static __always_inline void vma_rb_erase_ignore(struct vm_area_struct *vma,
						struct rb_root *root,
						struct vm_area_struct *ignore)
{
	
	validate_mm_rb(root, ignore);

	__vma_rb_erase(vma, root);
}

static __always_inline void vma_rb_erase(struct vm_area_struct *vma,
					 struct rb_root *root)
{
	vma_rb_erase_ignore(vma, root, vma);
}

static inline void
anon_vma_interval_tree_pre_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_remove(avc, &avc->anon_vma->rb_root);
}

static inline void
anon_vma_interval_tree_post_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_insert(avc, &avc->anon_vma->rb_root);
}

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
	vma_rb_insert(vma, &mm->mm_rb);
}

static void __vma_link_file(struct vm_area_struct *vma)
{
	struct file *file;

	file = vma->vm_file;
	if (file) {
		struct address_space *mapping = file->f_mapping;

		if (vma->vm_flags & VM_SHARED)
			mapping_allow_writable(mapping);

		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_insert(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
	}
}

static void
__vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
	struct vm_area_struct *prev, struct rb_node **rb_link,
	struct rb_node *rb_parent)
{
	__vma_link_list(mm, vma, prev);
	__vma_link_rb(mm, vma, rb_link, rb_parent);
}

static void vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
			struct vm_area_struct *prev, struct rb_node **rb_link,
			struct rb_node *rb_parent)
{
	struct address_space *mapping = NULL;

	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;
		i_mmap_lock_write(mapping);
	}

	__vma_link(mm, vma, prev, rb_link, rb_parent);
	__vma_link_file(vma);

	if (mapping)
		i_mmap_unlock_write(mapping);

	mm->map_count++;
	validate_mm(mm);
}

static __always_inline void __vma_unlink(struct mm_struct *mm,
						struct vm_area_struct *vma,
						struct vm_area_struct *ignore)
{
	vma_rb_erase_ignore(vma, &mm->mm_rb, ignore);
	__vma_unlink_list(mm, vma);
	
	vmacache_invalidate(mm);
}

int __vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert,
	struct vm_area_struct *expand)
{
	/* Minimal stub: simple VMA adjustment without complex merging */
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
	return 0;
}

static inline int is_mergeable_vma(struct vm_area_struct *vma,
				struct file *file, unsigned long vm_flags,
				struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
				struct anon_vma_name *anon_name)
{
	
	if ((vma->vm_flags ^ vm_flags) & ~VM_SOFTDIRTY)
		return 0;
	if (vma->vm_file != file)
		return 0;
	if (vma->vm_ops && vma->vm_ops->close)
		return 0;
	if (!is_mergeable_vm_userfaultfd_ctx(vma, vm_userfaultfd_ctx))
		return 0;
	if (!anon_vma_name_eq(anon_vma_name(vma), anon_name))
		return 0;
	return 1;
}

static inline int is_mergeable_anon_vma(struct anon_vma *anon_vma1,
					struct anon_vma *anon_vma2,
					struct vm_area_struct *vma)
{
	
	if ((!anon_vma1 || !anon_vma2) && (!vma ||
		list_is_singular(&vma->anon_vma_chain)))
		return 1;
	return anon_vma1 == anon_vma2;
}

static int
can_vma_merge_before(struct vm_area_struct *vma, unsigned long vm_flags,
		     struct anon_vma *anon_vma, struct file *file,
		     pgoff_t vm_pgoff,
		     struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		     struct anon_vma_name *anon_name)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx, anon_name) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		if (vma->vm_pgoff == vm_pgoff)
			return 1;
	}
	return 0;
}

static int
can_vma_merge_after(struct vm_area_struct *vma, unsigned long vm_flags,
		    struct anon_vma *anon_vma, struct file *file,
		    pgoff_t vm_pgoff,
		    struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		    struct anon_vma_name *anon_name)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx, anon_name) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		pgoff_t vm_pglen;
		vm_pglen = vma_pages(vma);
		if (vma->vm_pgoff + vm_pglen == vm_pgoff)
			return 1;
	}
	return 0;
}

struct vm_area_struct *vma_merge(struct mm_struct *mm,
			struct vm_area_struct *prev, unsigned long addr,
			unsigned long end, unsigned long vm_flags,
			struct anon_vma *anon_vma, struct file *file,
			pgoff_t pgoff, struct mempolicy *policy,
			struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
			struct anon_vma_name *anon_name)
{
	pgoff_t pglen = (end - addr) >> PAGE_SHIFT;
	struct vm_area_struct *area, *next;
	int err;

	
	if (vm_flags & VM_SPECIAL)
		return NULL;

	next = vma_next(mm, prev);
	area = next;
	if (area && area->vm_end == end)		
		next = next->vm_next;

	
	VM_WARN_ON(prev && addr <= prev->vm_start);
	VM_WARN_ON(area && end > area->vm_end);
	VM_WARN_ON(addr >= end);

	
	if (prev && prev->vm_end == addr &&
			mpol_equal(vma_policy(prev), policy) &&
			can_vma_merge_after(prev, vm_flags,
					    anon_vma, file, pgoff,
					    vm_userfaultfd_ctx, anon_name)) {
		
		if (next && end == next->vm_start &&
				mpol_equal(policy, vma_policy(next)) &&
				can_vma_merge_before(next, vm_flags,
						     anon_vma, file,
						     pgoff+pglen,
						     vm_userfaultfd_ctx, anon_name) &&
				is_mergeable_anon_vma(prev->anon_vma,
						      next->anon_vma, NULL)) {
							
			err = __vma_adjust(prev, prev->vm_start,
					 next->vm_end, prev->vm_pgoff, NULL,
					 prev);
		} else					
			err = __vma_adjust(prev, prev->vm_start,
					 end, prev->vm_pgoff, NULL, prev);
		if (err)
			return NULL;
		khugepaged_enter_vma(prev, vm_flags);
		return prev;
	}

	
	if (next && end == next->vm_start &&
			mpol_equal(policy, vma_policy(next)) &&
			can_vma_merge_before(next, vm_flags,
					     anon_vma, file, pgoff+pglen,
					     vm_userfaultfd_ctx, anon_name)) {
		if (prev && addr < prev->vm_end)	
			err = __vma_adjust(prev, prev->vm_start,
					 addr, prev->vm_pgoff, NULL, next);
		else {					
			err = __vma_adjust(area, addr, next->vm_end,
					 next->vm_pgoff - pglen, NULL, next);
			
			area = next;
		}
		if (err)
			return NULL;
		khugepaged_enter_vma(area, vm_flags);
		return area;
	}

	return NULL;
}

static int anon_vma_compatible(struct vm_area_struct *a, struct vm_area_struct *b)
{
	return a->vm_end == b->vm_start &&
		mpol_equal(vma_policy(a), vma_policy(b)) &&
		a->vm_file == b->vm_file &&
		!((a->vm_flags ^ b->vm_flags) & ~(VM_ACCESS_FLAGS | VM_SOFTDIRTY)) &&
		b->vm_pgoff == a->vm_pgoff + ((b->vm_start - a->vm_start) >> PAGE_SHIFT);
}

static struct anon_vma *reusable_anon_vma(struct vm_area_struct *old, struct vm_area_struct *a, struct vm_area_struct *b)
{
	if (anon_vma_compatible(a, b)) {
		struct anon_vma *anon_vma = READ_ONCE(old->anon_vma);

		if (anon_vma && list_is_singular(&old->anon_vma_chain))
			return anon_vma;
	}
	return NULL;
}

struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = NULL;

	
	if (vma->vm_next) {
		anon_vma = reusable_anon_vma(vma->vm_next, vma, vma->vm_next);
		if (anon_vma)
			return anon_vma;
	}

	
	if (vma->vm_prev)
		anon_vma = reusable_anon_vma(vma->vm_prev, vma->vm_prev, vma);

	
	return anon_vma;
}

static inline unsigned long round_hint_to_min(unsigned long hint)
{
	hint &= PAGE_MASK;
	if (((void *)hint != NULL) &&
	    (hint < mmap_min_addr))
		return PAGE_ALIGN(mmap_min_addr);
	return hint;
}

int mlock_future_check(struct mm_struct *mm, unsigned long flags,
		       unsigned long len)
{
	unsigned long locked, lock_limit;

	
	if (flags & VM_LOCKED) {
		locked = len >> PAGE_SHIFT;
		locked += mm->locked_vm;
		lock_limit = rlimit(RLIMIT_MEMLOCK);
		lock_limit >>= PAGE_SHIFT;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			return -EAGAIN;
	}
	return 0;
}

static inline u64 file_mmap_size_max(struct file *file, struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		return MAX_LFS_FILESIZE;

	if (S_ISBLK(inode->i_mode))
		return MAX_LFS_FILESIZE;

	if (S_ISSOCK(inode->i_mode))
		return MAX_LFS_FILESIZE;

	
	if (file->f_mode & FMODE_UNSIGNED_OFFSET)
		return 0;

	
	return ULONG_MAX;
}

static inline bool file_mmap_ok(struct file *file, struct inode *inode,
				unsigned long pgoff, unsigned long len)
{
	u64 maxsize = file_mmap_size_max(file, inode);

	if (maxsize && len > maxsize)
		return false;
	maxsize -= len;
	if (pgoff > maxsize >> PAGE_SHIFT)
		return false;
	return true;
}

unsigned long do_mmap(struct file *file, unsigned long addr,
			unsigned long len, unsigned long prot,
			unsigned long flags, unsigned long pgoff,
			unsigned long *populate, struct list_head *uf)
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

	if (!(flags & MAP_FIXED))
		addr = round_hint_to_min(addr);

	
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
		if (!can_do_mlock())
			return -EPERM;

	if (mlock_future_check(mm, vm_flags, len))
		return -EAGAIN;

	if (file) {
		struct inode *inode = file_inode(file);
		unsigned long flags_mask;

		if (!file_mmap_ok(file, inode, pgoff, len))
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
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}
	} else {
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
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

		
		if (file && is_file_hugepages(file))
			vm_flags |= VM_NORESERVE;
	}

	addr = mmap_region(file, addr, len, vm_flags, pgoff, uf);
	if (!IS_ERR_VALUE(addr) &&
	    ((vm_flags & VM_LOCKED) ||
	     (flags & (MAP_POPULATE | MAP_NONBLOCK)) == MAP_POPULATE))
		*populate = len;
	return addr;
}

unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff)
{
	struct file *file = NULL;
	unsigned long retval;

	if (!(flags & MAP_ANONYMOUS)) {
		audit_mmap_fd(fd, flags);
		file = fget(fd);
		if (!file)
			return -EBADF;
		if (is_file_hugepages(file)) {
			len = ALIGN(len, huge_page_size(hstate_file(file)));
		} else if (unlikely(flags & MAP_HUGETLB)) {
			retval = -EINVAL;
			goto out_fput;
		}
	} else if (flags & MAP_HUGETLB) {
		struct hstate *hs;

		hs = hstate_sizelog((flags >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK);
		if (!hs)
			return -EINVAL;

		len = ALIGN(len, huge_page_size(hs));
		
		file = hugetlb_file_setup(HUGETLB_ANON_FILE, len,
				VM_NORESERVE,
				HUGETLB_ANONHUGE_INODE,
				(flags >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	retval = vm_mmap_pgoff(file, addr, len, prot, flags, pgoff);
out_fput:
	if (file)
		fput(file);
	return retval;
}

SYSCALL_DEFINE6(mmap_pgoff, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, pgoff)
{
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
}

#ifdef __ARCH_WANT_SYS_OLD_MMAP
struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

SYSCALL_DEFINE1(old_mmap, struct mmap_arg_struct __user *, arg)
{
	struct mmap_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (offset_in_page(a.offset))
		return -EINVAL;

	return ksys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			       a.offset >> PAGE_SHIFT);
}
#endif 

/* Stubbed - not used externally */
int vma_wants_writenotify(struct vm_area_struct *vma, pgprot_t vm_page_prot)
{
	return 0;
}

unsigned long mmap_region(struct file *file, unsigned long addr,
		unsigned long len, vm_flags_t vm_flags, unsigned long pgoff,
		struct list_head *uf)
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
		if (call_mmap(file, vma)) {
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

static unsigned long unmapped_area(struct vm_unmapped_area_info *info)
{
	/* Simplified: use simple linear search from low_limit */
	struct mm_struct *mm = current->mm;
	unsigned long gap_start;

	/* Basic bounds checking */
	if (info->high_limit < info->length)
		return -ENOMEM;
	if (info->low_limit > info->high_limit - info->length)
		return -ENOMEM;

	/* Simple allocation from highest_vm_end or low_limit */
	gap_start = max(mm->highest_vm_end, info->low_limit);
	gap_start += (info->align_offset - gap_start) & info->align_mask;

	if (gap_start + info->length > info->high_limit)
		return -ENOMEM;

	return gap_start;
}

static unsigned long unmapped_area_topdown(struct vm_unmapped_area_info *info)
{
	/* Simplified: allocate from top of address space */
	struct mm_struct *mm = current->mm;
	unsigned long gap_end;

	/* Basic bounds checking */
	if (info->high_limit < info->length)
		return -ENOMEM;
	if (info->low_limit > info->high_limit - info->length)
		return -ENOMEM;

	/* Simple top-down allocation */
	gap_end = info->high_limit;
	if (mm->highest_vm_end <= info->high_limit)
		gap_end = mm->highest_vm_end;

	gap_end -= info->length;
	gap_end -= (gap_end - info->align_offset) & info->align_mask;

	if (gap_end < info->low_limit)
		return -ENOMEM;

	return gap_end;
}

unsigned long vm_unmapped_area(struct vm_unmapped_area_info *info)
{
	unsigned long addr;

	if (info->flags & VM_UNMAPPED_AREA_TOPDOWN)
		addr = unmapped_area_topdown(info);
	else
		addr = unmapped_area(info);

	return addr;
}

unsigned long
generic_get_unmapped_area(struct file *filp, unsigned long addr,
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
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		       unsigned long len, unsigned long pgoff,
		       unsigned long flags)
{
	return generic_get_unmapped_area(filp, addr, len, pgoff, flags);
}
#endif

unsigned long
generic_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
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
		VM_BUG_ON(addr != -ENOMEM);
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = mmap_end;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

#ifndef HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
unsigned long
arch_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
			       unsigned long len, unsigned long pgoff,
			       unsigned long flags)
{
	return generic_get_unmapped_area_topdown(filp, addr, len, pgoff, flags);
}
#endif

unsigned long
get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	unsigned long (*get_area)(struct file *, unsigned long,
				  unsigned long, unsigned long, unsigned long);

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

	error = security_mmap_addr(addr);
	return error ? error : addr;
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


struct vm_area_struct *
find_vma_prev(struct mm_struct *mm, unsigned long addr,
			struct vm_area_struct **pprev)
{
	struct vm_area_struct *vma;

	vma = find_vma(mm, addr);
	if (vma) {
		*pprev = vma->vm_prev;
	} else {
		struct rb_node *rb_node = rb_last(&mm->mm_rb);

		*pprev = rb_node ? rb_entry(rb_node, struct vm_area_struct, vm_rb) : NULL;
	}
	return vma;
}

static int acct_stack_growth(struct vm_area_struct *vma,
			     unsigned long size, unsigned long grow)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long new_start;

	
	if (!may_expand_vm(mm, vma->vm_flags, grow))
		return -ENOMEM;

	
	if (size > rlimit(RLIMIT_STACK))
		return -ENOMEM;

	
	if (mlock_future_check(mm, vma->vm_flags, grow << PAGE_SHIFT))
		return -ENOMEM;

	
	new_start = (vma->vm_flags & VM_GROWSUP) ? vma->vm_start :
			vma->vm_end - size;
	if (is_hugepage_only_range(vma->vm_mm, new_start, size))
		return -EFAULT;

	
	if (security_vm_enough_memory_mm(mm, grow))
		return -ENOMEM;

	return 0;
}

int expand_downwards(struct vm_area_struct *vma,
				   unsigned long address)
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
		if (grow <= vma->vm_pgoff) {
			error = acct_stack_growth(vma, size, grow);
			if (!error) {
				
				spin_lock(&mm->page_table_lock);
				if (vma->vm_flags & VM_LOCKED)
					mm->locked_vm += grow;
				vm_stat_account(mm, vma->vm_flags, grow);
				anon_vma_interval_tree_pre_update_vma(vma);
				vma->vm_start = address;
				vma->vm_pgoff -= grow;
				anon_vma_interval_tree_post_update_vma(vma);
				vma_gap_update(vma);
				spin_unlock(&mm->page_table_lock);

				perf_event_mmap(vma);
			}
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	khugepaged_enter_vma(vma, vma->vm_flags);
	validate_mm(mm);
	return error;
}

unsigned long stack_guard_gap = 256UL<<PAGE_SHIFT;

static int __init cmdline_parse_stack_guard_gap(char *p)
{
	unsigned long val;
	char *endptr;

	val = simple_strtoul(p, &endptr, 10);
	if (!*endptr)
		stack_guard_gap = val << PAGE_SHIFT;

	return 1;
}
__setup("stack_guard_gap=", cmdline_parse_stack_guard_gap);

int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	return expand_downwards(vma, address);
}

struct vm_area_struct *
find_extend_vma(struct mm_struct *mm, unsigned long addr)
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


static void remove_vma_list(struct mm_struct *mm, struct vm_area_struct *vma)
{
	unsigned long nr_accounted = 0;

	
	update_hiwater_vm(mm);
	do {
		long nrpages = vma_pages(vma);

		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += nrpages;
		vm_stat_account(mm, vma->vm_flags, -nrpages);
		vma = remove_vma(vma);
	} while (vma);
	vm_unacct_memory(nr_accounted);
	validate_mm(mm);
}

static void unmap_region(struct mm_struct *mm,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		unsigned long start, unsigned long end)
{
	struct vm_area_struct *next = vma_next(mm, prev);
	struct mmu_gather tlb;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm);
	update_hiwater_rss(mm);
	unmap_vmas(&tlb, vma, start, end);
	free_pgtables(&tlb, vma, prev ? prev->vm_end : FIRST_USER_ADDRESS,
				 next ? next->vm_start : USER_PGTABLES_CEILING);
	tlb_finish_mmu(&tlb);
}

static bool
detach_vmas_to_be_unmapped(struct mm_struct *mm, struct vm_area_struct *vma,
	struct vm_area_struct *prev, unsigned long end)
{
	struct vm_area_struct **insertion_point;
	struct vm_area_struct *tail_vma = NULL;

	insertion_point = (prev ? &prev->vm_next : &mm->mmap);
	vma->vm_prev = NULL;
	do {
		vma_rb_erase(vma, &mm->mm_rb);
		if (vma->vm_flags & VM_LOCKED)
			mm->locked_vm -= vma_pages(vma);
		mm->map_count--;
		tail_vma = vma;
		vma = vma->vm_next;
	} while (vma && vma->vm_start < end);
	*insertion_point = vma;
	if (vma) {
		vma->vm_prev = prev;
		vma_gap_update(vma);
	} else
		mm->highest_vm_end = prev ? vm_end_gap(prev) : 0;
	tail_vma->vm_next = NULL;

	
	vmacache_invalidate(mm);

	
	if (vma && (vma->vm_flags & VM_GROWSDOWN))
		return false;
	if (prev && (prev->vm_flags & VM_GROWSUP))
		return false;
	return true;
}

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

	err = vma_dup_policy(vma, new);
	if (err)
		goto out_free_vma;

	err = anon_vma_clone(new, vma);
	if (err)
		goto out_free_mpol;

	if (new->vm_file)
		get_file(new->vm_file);

	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

	if (new_below)
		err = vma_adjust(vma, addr, vma->vm_end, vma->vm_pgoff +
			((addr - new->vm_start) >> PAGE_SHIFT), new);
	else
		err = vma_adjust(vma, vma->vm_start, addr, vma->vm_pgoff, new);

	
	if (!err)
		return 0;

	
	if (new->vm_ops && new->vm_ops->close)
		new->vm_ops->close(new);
	if (new->vm_file)
		fput(new->vm_file);
	unlink_anon_vmas(new);
 out_free_mpol:
	mpol_put(vma_policy(new));
 out_free_vma:
	vm_area_free(new);
	return err;
}

int split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
	      unsigned long addr, int new_below)
{
	if (mm->map_count >= sysctl_max_map_count)
		return -ENOMEM;

	return __split_vma(mm, vma, addr, new_below);
}

int __do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
		struct list_head *uf, bool downgrade)
{
	unsigned long end;
	struct vm_area_struct *vma, *prev, *last;

	if ((offset_in_page(start)) || start > TASK_SIZE || len > TASK_SIZE-start)
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

	if (unlikely(uf)) {
		
		int error = userfaultfd_unmap_prep(vma, start, end, uf);
		if (error)
			return error;
	}

	
	if (!detach_vmas_to_be_unmapped(mm, vma, prev, end))
		downgrade = false;

	if (downgrade)
		mmap_write_downgrade(mm);

	unmap_region(mm, vma, prev, start, end);

	
	remove_vma_list(mm, vma);

	return downgrade ? 1 : 0;
}

int do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
	      struct list_head *uf)
{
	return __do_munmap(mm, start, len, uf, false);
}

static int __vm_munmap(unsigned long start, size_t len, bool downgrade)
{
	int ret;
	struct mm_struct *mm = current->mm;
	LIST_HEAD(uf);

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = __do_munmap(mm, start, len, &uf, downgrade);
	
	if (ret == 1) {
		mmap_read_unlock(mm);
		ret = 0;
	} else
		mmap_write_unlock(mm);

	userfaultfd_unmap_complete(mm, &uf);
	return ret;
}

int vm_munmap(unsigned long start, size_t len)
{
	return __vm_munmap(start, len, false);
}

SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
{
	addr = untagged_addr(addr);
	return __vm_munmap(addr, len, true);
}

SYSCALL_DEFINE5(remap_file_pages, unsigned long, start, unsigned long, size,
		unsigned long, prot, unsigned long, pgoff, unsigned long, flags)
{
	return -ENOSYS;
}

static int do_brk_flags(unsigned long addr, unsigned long len, unsigned long flags, struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct rb_node **rb_link, *rb_parent;
	pgoff_t pgoff = addr >> PAGE_SHIFT;
	int error;
	unsigned long mapped_addr;

	
	if ((flags & (~VM_EXEC)) != 0)
		return -EINVAL;
	flags |= VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT | mm->def_flags;

	mapped_addr = get_unmapped_area(NULL, addr, len, 0, MAP_FIXED);
	if (IS_ERR_VALUE(mapped_addr))
		return mapped_addr;

	error = mlock_future_check(mm, mm->def_flags, len);
	if (error)
		return error;

	
	if (munmap_vma_range(mm, addr, len, &prev, &rb_link, &rb_parent, uf))
		return -ENOMEM;

	
	if (!may_expand_vm(mm, flags, len >> PAGE_SHIFT))
		return -ENOMEM;

	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;

	if (security_vm_enough_memory_mm(mm, len >> PAGE_SHIFT))
		return -ENOMEM;

	
	vma = vma_merge(mm, prev, addr, addr + len, flags,
			NULL, NULL, pgoff, NULL, NULL_VM_UFFD_CTX, NULL);
	if (vma)
		goto out;

	
	vma = vm_area_alloc(mm);
	if (!vma) {
		vm_unacct_memory(len >> PAGE_SHIFT);
		return -ENOMEM;
	}

	vma_set_anonymous(vma);
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_pgoff = pgoff;
	vma->vm_flags = flags;
	vma->vm_page_prot = vm_get_page_prot(flags);
	vma_link(mm, vma, prev, rb_link, rb_parent);
out:
	perf_event_mmap(vma);
	mm->total_vm += len >> PAGE_SHIFT;
	mm->data_vm += len >> PAGE_SHIFT;
	if (flags & VM_LOCKED)
		mm->locked_vm += (len >> PAGE_SHIFT);
	vma->vm_flags |= VM_SOFTDIRTY;
	return 0;
}

int vm_brk_flags(unsigned long addr, unsigned long request, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	unsigned long len;
	int ret;
	bool populate;
	LIST_HEAD(uf);

	len = PAGE_ALIGN(request);
	if (len < request)
		return -ENOMEM;
	if (!len)
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = do_brk_flags(addr, len, flags, &uf);
	populate = ((mm->def_flags & VM_LOCKED) != 0);
	mmap_write_unlock(mm);
	userfaultfd_unmap_complete(mm, &uf);
	if (populate && !ret)
		mm_populate(addr, len);
	return ret;
}

int vm_brk(unsigned long addr, unsigned long len)
{
	return vm_brk_flags(addr, len, 0);
}

void exit_mmap(struct mm_struct *mm)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma;
	unsigned long nr_accounted = 0;

	
	mmu_notifier_release(mm);

	if (unlikely(mm_is_oom_victim(mm))) {
		
		(void)__oom_reap_task_mm(mm);
		set_bit(MMF_OOM_SKIP, &mm->flags);
	}

	mmap_write_lock(mm);
	arch_exit_mmap(mm);

	vma = mm->mmap;
	if (!vma) {
		
		mmap_write_unlock(mm);
		return;
	}

	lru_add_drain();
	flush_cache_mm(mm);
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

	if (find_vma_links(mm, vma->vm_start, vma->vm_end,
			   &prev, &rb_link, &rb_parent))
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

struct vm_area_struct *copy_vma(struct vm_area_struct **vmap,
	unsigned long addr, unsigned long len, pgoff_t pgoff,
	bool *need_rmap_locks)
{
	struct vm_area_struct *vma = *vmap;
	unsigned long vma_start = vma->vm_start;
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *new_vma, *prev;
	struct rb_node **rb_link, *rb_parent;
	bool faulted_in_anon_vma = true;

	
	if (unlikely(vma_is_anonymous(vma) && !vma->anon_vma)) {
		pgoff = addr >> PAGE_SHIFT;
		faulted_in_anon_vma = false;
	}

	if (find_vma_links(mm, addr, addr + len, &prev, &rb_link, &rb_parent))
		return NULL;	
	new_vma = vma_merge(mm, prev, addr, addr + len, vma->vm_flags,
			    vma->anon_vma, vma->vm_file, pgoff, vma_policy(vma),
			    vma->vm_userfaultfd_ctx, anon_vma_name(vma));
	if (new_vma) {
		
		if (unlikely(vma_start >= new_vma->vm_start &&
			     vma_start < new_vma->vm_end)) {
			
			VM_BUG_ON_VMA(faulted_in_anon_vma, new_vma);
			*vmap = vma = new_vma;
		}
		*need_rmap_locks = (new_vma->vm_pgoff <= vma->vm_pgoff);
	} else {
		new_vma = vm_area_dup(vma);
		if (!new_vma)
			goto out;
		new_vma->vm_start = addr;
		new_vma->vm_end = addr + len;
		new_vma->vm_pgoff = pgoff;
		if (vma_dup_policy(vma, new_vma))
			goto out_free_vma;
		if (anon_vma_clone(new_vma, vma))
			goto out_free_mempol;
		if (new_vma->vm_file)
			get_file(new_vma->vm_file);
		if (new_vma->vm_ops && new_vma->vm_ops->open)
			new_vma->vm_ops->open(new_vma);
		vma_link(mm, new_vma, prev, rb_link, rb_parent);
		*need_rmap_locks = false;
	}
	return new_vma;

out_free_mempol:
	mpol_put(vma_policy(new_vma));
out_free_vma:
	vm_area_free(new_vma);
out:
	return NULL;
}

bool may_expand_vm(struct mm_struct *mm, vm_flags_t flags, unsigned long npages)
{
	if (mm->total_vm + npages > rlimit(RLIMIT_AS) >> PAGE_SHIFT)
		return false;

	if (is_data_mapping(flags) &&
	    mm->data_vm + npages > rlimit(RLIMIT_DATA) >> PAGE_SHIFT) {
		
		if (rlimit(RLIMIT_DATA) == 0 &&
		    mm->data_vm + npages <= rlimit_max(RLIMIT_DATA) >> PAGE_SHIFT)
			return true;


		if (!ignore_rlimit_data)
			return false;
	}

	return true;
}

void vm_stat_account(struct mm_struct *mm, vm_flags_t flags, long npages)
{
	WRITE_ONCE(mm->total_vm, READ_ONCE(mm->total_vm)+npages);

	if (is_exec_mapping(flags))
		mm->exec_vm += npages;
	else if (is_stack_mapping(flags))
		mm->stack_vm += npages;
	else if (is_data_mapping(flags))
		mm->data_vm += npages;
}

static vm_fault_t special_mapping_fault(struct vm_fault *vmf);

static void special_mapping_close(struct vm_area_struct *vma)
{
}

static const char *special_mapping_name(struct vm_area_struct *vma)
{
	return ((struct vm_special_mapping *)vma->vm_private_data)->name;
}

static int special_mapping_mremap(struct vm_area_struct *new_vma)
{
	struct vm_special_mapping *sm = new_vma->vm_private_data;

	if (WARN_ON_ONCE(current->mm != new_vma->vm_mm))
		return -EFAULT;

	if (sm->mremap)
		return sm->mremap(sm, new_vma);

	return 0;
}

static int special_mapping_split(struct vm_area_struct *vma, unsigned long addr)
{
	
	return -EINVAL;
}

static const struct vm_operations_struct special_mapping_vmops = {
	.close = special_mapping_close,
	.fault = special_mapping_fault,
	.mremap = special_mapping_mremap,
	.name = special_mapping_name,
	
	.access = NULL,
	.may_split = special_mapping_split,
};

static const struct vm_operations_struct legacy_special_mapping_vmops = {
	.close = special_mapping_close,
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

static struct vm_area_struct *__install_special_mapping(
	struct mm_struct *mm,
	unsigned long addr, unsigned long len,
	unsigned long vm_flags, void *priv,
	const struct vm_operations_struct *ops)
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

	vma->vm_ops = ops;
	vma->vm_private_data = priv;

	ret = insert_vm_struct(mm, vma);
	if (ret)
		goto out;

	vm_stat_account(mm, vma->vm_flags, len >> PAGE_SHIFT);

	perf_event_mmap(vma);

	return vma;

out:
	vm_area_free(vma);
	return ERR_PTR(ret);
}

bool vma_is_special_mapping(const struct vm_area_struct *vma,
	const struct vm_special_mapping *sm)
{
	return vma->vm_private_data == sm &&
		(vma->vm_ops == &special_mapping_vmops ||
		 vma->vm_ops == &legacy_special_mapping_vmops);
}

struct vm_area_struct *_install_special_mapping(
	struct mm_struct *mm,
	unsigned long addr, unsigned long len,
	unsigned long vm_flags, const struct vm_special_mapping *spec)
{
	return __install_special_mapping(mm, addr, len, vm_flags, (void *)spec,
					&special_mapping_vmops);
}

int install_special_mapping(struct mm_struct *mm,
			    unsigned long addr, unsigned long len,
			    unsigned long vm_flags, struct page **pages)
{
	struct vm_area_struct *vma = __install_special_mapping(
		mm, addr, len, vm_flags, (void *)pages,
		&legacy_special_mapping_vmops);

	return PTR_ERR_OR_ZERO(vma);
}

static DEFINE_MUTEX(mm_all_locks_mutex);

int mm_take_all_locks(struct mm_struct *mm)
{
	/* Stub: mm_take_all_locks not needed for minimal kernel */
	mutex_lock(&mm_all_locks_mutex);
	return 0;
}

void mm_drop_all_locks(struct mm_struct *mm)
{
	/* Stub: mm_drop_all_locks not needed for minimal kernel */
	mutex_unlock(&mm_all_locks_mutex);
}

void __init mmap_init(void)
{
	int ret;

	ret = percpu_counter_init(&vm_committed_as, 0, GFP_KERNEL);
	VM_BUG_ON(ret);
}

static int init_user_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_user_reserve_kbytes = min(free_kbytes / 32, 1UL << 17);
	return 0;
}
subsys_initcall(init_user_reserve);

static int init_admin_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_admin_reserve_kbytes = min(free_kbytes / 32, 1UL << 13);
	return 0;
}
subsys_initcall(init_admin_reserve);

static int reserve_mem_notifier(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	/* Stub: hotmemory reserve notifier not needed for minimal kernel */
	return NOTIFY_OK;
}

static struct notifier_block reserve_mem_nb = {
	.notifier_call = reserve_mem_notifier,
};

static int __meminit init_reserve_notifier(void)
{
	if (register_hotmemory_notifier(&reserve_mem_nb))
		pr_err("Failed registering memory add/remove notifier for admin reserve\n");

	return 0;
}
subsys_initcall(init_reserve_notifier);
