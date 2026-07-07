
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/rmap.h>
#include <linux/pkeys.h>
#include <linux/sched/mm.h>
#include <linux/oom.h>


#include "internal.h"

#ifndef arch_mmap_check
#define arch_mmap_check(addr, len, flags)	(0)
#endif

int mmap_rnd_bits __read_mostly = CONFIG_ARCH_MMAP_RND_BITS;

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

/* Removed: vm_pgprot_modify, vma_set_page_prot - never called */


#define validate_mm_rb(root, ignore) do { } while (0)
#define validate_mm(mm) do { } while (0)

static inline void vma_rb_insert(struct vm_area_struct *vma,
				 struct rb_root *root)
{

	validate_mm_rb(root, NULL);

	rb_insert_color(&vma->vm_rb, root);
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

static inline int
munmap_vma_range(struct mm_struct *mm, unsigned long start, unsigned long len,
		 struct vm_area_struct **pprev, struct rb_node ***link,
		 struct rb_node **parent)
{

	while (find_vma_links(mm, start, start + len, pprev, link, parent))
		if (__do_munmap(mm, start, len))
			return -ENOMEM;

	return 0;
}

void __vma_link_rb(struct mm_struct *mm, struct vm_area_struct *vma,
		struct rb_node **rb_link, struct rb_node *rb_parent)
{
	rb_link_node(&vma->vm_rb, rb_parent, rb_link);
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

		vma_interval_tree_insert(vma, &mapping->i_mmap);
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

static struct anon_vma *reusable_anon_vma(struct vm_area_struct *old, struct vm_area_struct *a, struct vm_area_struct *b)
{
	/*
	 * SAFE-FALLBACK stub: returns NULL so find_mergeable_anon_vma() never
	 * reuses an adjacent VMA's anon_vma. The sole consumer
	 * (__anon_vma_prepare) allocates a fresh anon_vma when NULL is returned,
	 * so behavior is preserved (only the merge optimization is skipped).
	 * Runtime-dead on this boot-once-and-print artifact.
	 */
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

/* Used internally by acct_stack_growth */
static inline u64 file_mmap_size_max(struct file *file, struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		return MAX_LFS_FILESIZE;

	if (S_ISBLK(inode->i_mode))
		return MAX_LFS_FILESIZE;

	if (S_ISSOCK(inode->i_mode))
		return MAX_LFS_FILESIZE;

	/* No file_operations sets FMODE_UNSIGNED_OFFSET on this build. */
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
			unsigned long *populate)
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

	/*
	 * MAP_TYPE is always MAP_PRIVATE on this build: there is no mmap(2)
	 * syscall (removed from syscall_32.tbl) and the only in-kernel callers
	 * (binfmt_elf via vm_mmap) pass MAP_PRIVATE[|MAP_FIXED]. So the
	 * MAP_SHARED / MAP_SHARED_VALIDATE arms are dead, and so is the
	 * MAP_LOCKED check (MAP_LOCKED is never set, and can_do_mlock() is a
	 * permanent false stub). Fold to the MAP_PRIVATE path.
	 */
	if (file) {
		struct inode *inode = file_inode(file);

		if (!file_mmap_ok(file, inode, pgoff, len))
			return -EOVERFLOW;

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

	addr = mmap_region(file, addr, len, vm_flags, pgoff);
	/*
	 * VM_LOCKED is never set (no mmap syscall, ELF loader passes only
	 * MAP_PRIVATE/MAP_FIXED) and MAP_POPULATE is never passed, so *populate
	 * stays 0 (initialised above); the populate path is dead.
	 */
	return addr;
}


/* Removed: vma_wants_writenotify - was used only by vma_set_page_prot (~4 LOC) */

unsigned long mmap_region(struct file *file, unsigned long addr,
		unsigned long len, vm_flags_t vm_flags, unsigned long pgoff)
{
	/* Minimal stub: simplified mmap without complex VMA merging/splitting */
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct rb_node **rb_link, *rb_parent;
	struct vm_area_struct *prev = NULL;

	if (munmap_vma_range(mm, addr, len, &prev, &rb_link, &rb_parent))
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

/*
 * Runtime-dead on a 1-shot boot: arch_pick_mmap_layout selects the topdown
 * get_unmapped_area, whose addr fast-path always returns before reaching
 * vm_unmapped_area; the legacy bottom-up arch_get_unmapped_area is never
 * assigned. Body stubbed (symbol kept link-live for the mm.h extern); the
 * private unmapped_area / unmapped_area_topdown helpers are deleted.
 */
unsigned long vm_unmapped_area(void)
{
	return -ENOMEM;
}

#ifndef HAVE_ARCH_UNMAPPED_AREA
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		       unsigned long len, unsigned long pgoff,
		       unsigned long flags)
{
	/*
	 * Legacy bottom-up layout is never selected on this boot
	 * (arch_pick_mmap_layout uses the topdown variant); never assigned,
	 * never called. Body stubbed, symbol kept link-live.
	 */
	return -ENOMEM;
}
#endif

unsigned long
generic_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
				  unsigned long flags)
{
	struct vm_area_struct *vma, *prev;
	struct mm_struct *mm = current->mm;
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

	addr = vm_unmapped_area();


	if (offset_in_page(addr)) {
		VM_BUG_ON(addr != -ENOMEM);
		addr = vm_unmapped_area();
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

	/*
	 * MAP_SHARED is never set on this build (do_mmap is the sole caller and
	 * the only mmap path is binfmt_elf via vm_mmap with MAP_PRIVATE[|MAP_FIXED]),
	 * so the shmem_get_unmapped_area arm is dead and folded out.
	 */
	get_area = current->mm->get_unmapped_area;
	if (file) {
		if (file->f_op->get_unmapped_area)
			get_area = file->f_op->get_unmapped_area;
	}

	addr = get_area(file, addr, len, pgoff, flags);
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
	struct vm_area_struct *vma = NULL;

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


/* Used by arch_get_unmapped_area and generic_get_unmapped_area_topdown */
struct vm_area_struct *
find_vma_prev(struct mm_struct *mm, unsigned long addr,
			struct vm_area_struct **pprev)
{
	*pprev = NULL;
	return find_vma(mm, addr);
}

static int acct_stack_growth(struct vm_area_struct *vma,
			     unsigned long size, unsigned long grow)
{
	struct mm_struct *mm = vma->vm_mm;


	if (size > rlimit(RLIMIT_STACK))
		return -ENOMEM;


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
	
	if (prev && vma_is_accessible(prev)) {
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
				anon_vma_interval_tree_pre_update_vma(vma);
				vma->vm_start = address;
				vma->vm_pgoff -= grow;
				anon_vma_interval_tree_post_update_vma(vma);
				spin_unlock(&mm->page_table_lock);
			}
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	validate_mm(mm);
	return error;
}

unsigned long stack_guard_gap = 256UL<<PAGE_SHIFT;

int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	return expand_downwards(vma, address);
}

struct vm_area_struct *
find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;

	addr &= PAGE_MASK;
	vma = find_vma(mm, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	return NULL;
}


int __do_munmap(struct mm_struct *mm, unsigned long start, size_t len)
{
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: this kernel's only job is boot+print+
	 * stay-alive; it never unmaps a region.  __do_munmap is link-live via
	 * fs/binfmt_elf.c (ELF loader, never runs on this boot) and via the
	 * mmap_region->munmap_vma_range overlap path (find_vma_links never finds
	 * an overlap on this boot, so this is never reached).  Returning 0 is the
	 * success contract the sole live caller expects.  Stubbing the body made
	 * its private subtree (__split_vma, detach_vmas_to_be_unmapped,
	 * unmap_region, remove_vma_list) dead -> all deleted.
	 */
	return 0;
}

int vm_brk_flags(unsigned long addr, unsigned long request, unsigned long flags)
{
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: this kernel's only job is boot+print+
	 * stay-alive; it never brk()s an anonymous region.  vm_brk_flags is
	 * link-live via fs/binfmt_elf.c (the ELF loader's BSS setup, which
	 * never runs on this boot) + the mm.h:859 extern.  Trace HIT=False.
	 * Returning 0 is the success contract the sole live caller expects.
	 * Stubbing the body made its private subtree (do_brk_flags, vma_merge,
	 * can_vma_merge_before/after, vma_next, is_mergeable_vma/anon_vma) dead
	 * -> all deleted.  __vma_adjust kept (link-live via fs/exec.c).
	 */
	return 0;
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

void __init mmap_init(void)
{
}

