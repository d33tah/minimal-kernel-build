

#include <linux/mman.h>
#include <linux/file.h>
#include <linux/security.h>
#include <linux/sched/signal.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

#include "internal.h"

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

#define validate_mm(mm) \
	do {            \
	} while (0)

/*
 * The VMAs of an mm are kept only as the address-sorted mm->mmap list (no
 * rbtree): a process here has a handful of VMAs (the ELF segments, bss and
 * the stack), so lookups walk the list.
 */
static int find_vma_links(struct mm_struct *mm, unsigned long addr,
			  unsigned long end, struct vm_area_struct **pprev)
{
	struct vm_area_struct *vma, *prev = NULL;

	mmap_assert_locked(mm);
	for (vma = mm->mmap; vma; prev = vma, vma = vma->vm_next) {
		if (vma->vm_end > addr) {
			if (vma->vm_start < end)
				return -ENOMEM;
			break;
		}
	}
	*pprev = prev;
	return 0;
}

static inline int munmap_vma_range(struct mm_struct *mm, unsigned long start,
				   unsigned long len,
				   struct vm_area_struct **pprev)
{
	while (find_vma_links(mm, start, start + len, pprev))
		if (__do_munmap(mm, start, len))
			return -ENOMEM;

	return 0;
}

static void vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
		     struct vm_area_struct *prev)
{
	__vma_link_list(mm, vma, prev);
	mm->map_count++;
	validate_mm(mm);
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
	return pgoff <= maxsize >> PAGE_SHIFT;
}

unsigned long do_mmap(struct file *file, unsigned long addr, unsigned long len,
		      unsigned long prot, unsigned long flags,
		      unsigned long pgoff, unsigned long *populate)
{
	struct mm_struct *mm = current->mm;
	vm_flags_t vm_flags;

	*populate = 0;

	if (!len)
		return -EINVAL;

	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		prot |= PROT_EXEC;

	if (flags & MAP_FIXED_NOREPLACE)
		flags |= MAP_FIXED;

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

	vm_flags = calc_vm_prot_bits(prot) | calc_vm_flag_bits(flags) |
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
			  unsigned long len, vm_flags_t vm_flags,
			  unsigned long pgoff)
{
	/* Minimal stub: simplified mmap without complex VMA merging/splitting */
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_area_struct *prev = NULL;

	if (munmap_vma_range(mm, addr, len, &prev))
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

	vma_link(mm, vma, prev);
	return addr;
}

/*
 * Every mapping in this kernel is placed at a fixed address: there is no
 * mmap(2) and the ELF loader maps the PT_LOAD segments of ET_EXEC binaries
 * MAP_FIXED[_NOREPLACE] (do_mmap turns the latter into MAP_FIXED). So there
 * is no free-area search, no mmap_base and no layout randomization; a
 * request without MAP_FIXED fails.
 */
unsigned long get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags)
{
	if (!(flags & MAP_FIXED))
		return -ENOMEM;

	if (len > TASK_SIZE - mmap_min_addr)
		return -ENOMEM;

	if (addr > TASK_SIZE - len)
		return -ENOMEM;
	if (offset_in_page(addr))
		return -EINVAL;

	return addr;
}

struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;

	mmap_assert_locked(mm);
	for (vma = mm->mmap; vma; vma = vma->vm_next)
		if (vma->vm_end > addr)
			return vma;
	return NULL;
}

static int acct_stack_growth(struct vm_area_struct *vma, unsigned long size,
			     unsigned long grow)
{
	struct mm_struct *mm = vma->vm_mm;

	if (size > rlimit(RLIMIT_STACK))
		return -ENOMEM;

	if (security_vm_enough_memory_mm(mm, grow))
		return -ENOMEM;

	return 0;
}

static int expand_downwards(struct vm_area_struct *vma, unsigned long address)
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

	if (address < vma->vm_start) {
		unsigned long size, grow;

		size = vma->vm_end - address;
		grow = (vma->vm_start - address) >> PAGE_SHIFT;

		error = -ENOMEM;
		if (grow <= vma->vm_pgoff) {
			error = acct_stack_growth(vma, size, grow);
			if (!error) {
				spin_lock(&mm->page_table_lock);
				vma->vm_start = address;
				vma->vm_pgoff -= grow;
				spin_unlock(&mm->page_table_lock);
			}
		}
	}
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

	addr &= PAGE_MASK;
	vma = find_vma(mm, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	return NULL;
}

/*
 * Clear the PTEs of [start, end) in @vma and drop each page's mapping
 * reference: anonymous pages go back to the free lists, page-cache pages keep
 * the cache's own reference. Only current->mm is
 * ever unmapped (mmap_region during exec), so invlpg flushes its entries.
 */
static void zap_vma_range(struct vm_area_struct *vma, unsigned long start,
			  unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	for (; start < end; start += PAGE_SIZE) {
		pgd_t *pgd = pgd_offset(mm, start);
		pmd_t *pmd = pmd_offset(
			pud_offset(p4d_offset(pgd, start), start), start);
		struct page *page = NULL;
		spinlock_t *ptl;
		pte_t *pte;

		if (pmd_none(*pmd))
			continue;
		pte = pte_offset_map_lock(mm, pmd, start, &ptl);
		if (!pte_none(*pte))
			page = vm_normal_page(vma, start, *pte);
		pte_clear(mm, start, pte);
		pte_unmap_unlock(pte, ptl);
		flush_tlb_one_user(start);
		if (page) {
			page_remove_rmap(page);
			put_page(page);
		}
	}
}

/*
 * Free the page tables in [start, end) that no VMA covers any more (2-level
 * paging: one PTE page per 4 MiB PMD slot, already emptied by zap_vma_range).
 */
static void free_unused_pgtables(struct mm_struct *mm, unsigned long start,
				 unsigned long end)
{
	unsigned long addr;

	for (addr = start & PMD_MASK; addr < end; addr += PMD_SIZE) {
		struct vm_area_struct *vma = find_vma(mm, addr);
		pmd_t *pmd = pmd_offset(
			pud_offset(p4d_offset(pgd_offset(mm, addr), addr),
				   addr),
			addr);
		struct page *pte;

		if (pmd_none(*pmd) || (vma && vma->vm_start < addr + PMD_SIZE))
			continue;
		pte = virt_to_page((void *)pmd_page_vaddr(*pmd));
		set_pmd(pmd, __pmd(0));
		flush_tlb_mm_range(mm, addr, addr + PMD_SIZE, PAGE_SHIFT, true);
		pte_free(mm, pte);
	}
}

/*
 * Remove [start, start + len) from @mm: VMAs inside the range are freed,
 * VMAs straddling an end are trimmed, and a VMA covering the whole range is
 * split in two first.
 */
int __do_munmap(struct mm_struct *mm, unsigned long start, size_t len)
{
	unsigned long end = start + PAGE_ALIGN(len);
	struct vm_area_struct *vma, *next;

	if (offset_in_page(start) || end <= start || end > TASK_SIZE)
		return -EINVAL;

	for (vma = find_vma(mm, start); vma && vma->vm_start < end;
	     vma = next) {
		if (vma->vm_start < start && vma->vm_end > end) {
			struct vm_area_struct *tail = vm_area_alloc(mm);

			if (!tail)
				return -ENOMEM;
			*tail = *vma;
			tail->vm_start = end;
			tail->vm_pgoff += (end - vma->vm_start) >> PAGE_SHIFT;
			if (tail->vm_file)
				get_file(tail->vm_file);
			vma->vm_end = end;
			insert_vm_struct(mm, tail);
		}
		next = vma->vm_next;
		zap_vma_range(vma, max(start, vma->vm_start),
			      min(end, vma->vm_end));
		if (vma->vm_start < start) {
			vma->vm_end = start;
		} else if (vma->vm_end > end) {
			vma->vm_pgoff += (end - vma->vm_start) >> PAGE_SHIFT;
			vma->vm_start = end;
		} else {
			if (vma->vm_prev)
				vma->vm_prev->vm_next = next;
			else
				mm->mmap = next;
			if (next)
				next->vm_prev = vma->vm_prev;
			mm->map_count--;
			if (vma->vm_file)
				fput(vma->vm_file);
			vm_area_free(vma);
		}
	}
	free_unused_pgtables(mm, start, end);
	return 0;
}

/*
 * Map the zero-filled part of an ELF segment (p_memsz > p_filesz) as a
 * private anonymous region; do_anonymous_page supplies zeroed pages on
 * first touch. MAP_FIXED_NOREPLACE: an overlap with an existing mapping
 * (e.g. the stack) fails with -EEXIST instead of unmapping it.
 */
int vm_brk_flags(unsigned long addr, unsigned long request, unsigned long flags)
{
	unsigned long prot = PROT_READ | PROT_WRITE;
	unsigned long ret;

	if (flags & ~VM_EXEC)
		return -EINVAL;
	if (flags & VM_EXEC)
		prot |= PROT_EXEC;

	/* No file: do_mmap/mmap_region make it an anonymous mapping. */
	ret = vm_mmap(NULL, addr, request, prot,
		      MAP_PRIVATE | MAP_FIXED_NOREPLACE, 0);
	return IS_ERR_VALUE(ret) ? (int)ret : 0;
}

int insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *prev;

	if (find_vma_links(mm, vma->vm_start, vma->vm_end, &prev))
		return -ENOMEM;
	if ((vma->vm_flags & VM_ACCOUNT) &&
	    security_vm_enough_memory_mm(mm, vma_pages(vma)))
		return -ENOMEM;

	if (vma_is_anonymous(vma))
		vma->vm_pgoff = vma->vm_start >> PAGE_SHIFT;

	vma_link(mm, vma, prev);
	return 0;
}
