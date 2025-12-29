// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal NOMMU implementation for Hello World kernel
 * Uses simple bump allocator - no deallocation support
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include <linux/rmap.h>

#include <asm/tlb.h>
#include <asm/tlbflush.h>

/* Bump allocator state */
static unsigned long bump_current;
static unsigned long bump_end;

/*
 * Initialize bump allocator with a memory region
 * Called during boot after memory is detected
 */
void __init nommu_bump_init(unsigned long start, unsigned long end)
{
	bump_current = PAGE_ALIGN(start);
	bump_end = end & PAGE_MASK;
	pr_info("NOMMU: bump allocator %lx - %lx (%lu KB)\n", bump_current,
		bump_end, (bump_end - bump_current) >> 10);
}

/*
 * Simple bump allocation - just advance pointer
 */
unsigned long nommu_bump_alloc(unsigned long size)
{
	unsigned long addr;

	size = PAGE_ALIGN(size);
	if (bump_current + size > bump_end)
		return 0;

	addr = bump_current;
	bump_current += size;
	return addr;
}

/* VMA operations - vm_area_alloc/dup/free are in fork.c */
void vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
	      struct vm_area_struct *prev, struct rb_node **rb_link,
	      struct rb_node *rb_parent)
{
}

/* VMA lookup - always return NULL (no VMAs in NOMMU) */
struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
}
EXPORT_SYMBOL(find_vma);

struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
}

/* Page fault handling - should not happen in our simple kernel */
vm_fault_t handle_mm_fault(struct vm_area_struct *vma, unsigned long address,
			   unsigned int flags, struct pt_regs *regs)
{
	return VM_FAULT_SIGBUS;
}
EXPORT_SYMBOL(handle_mm_fault);

int fixup_user_fault(struct mm_struct *mm, unsigned long address,
		     unsigned int fault_flags, bool *unlocked)
{
	return -EFAULT;
}
EXPORT_SYMBOL(fixup_user_fault);

/* Memory syscalls - all stubs */
SYSCALL_DEFINE6(mmap_pgoff, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags, unsigned long, fd,
		unsigned long, pgoff)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
{
	return -ENOSYS;
}

SYSCALL_DEFINE1(brk, unsigned long, brk)
{
	return 0;
}

SYSCALL_DEFINE5(mremap, unsigned long, addr, unsigned long, old_len,
		unsigned long, new_len, unsigned long, flags, unsigned long,
		new_addr)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(msync, unsigned long, start, size_t, len, int, flags)
{
	return 0;
}

SYSCALL_DEFINE3(madvise, unsigned long, start, size_t, len_in, int, behavior)
{
	return 0;
}

SYSCALL_DEFINE4(remap_file_pages, unsigned long, start, unsigned long, size,
		unsigned long, prot, unsigned long, pgoff, unsigned long, flags)
{
	return -ENOSYS;
}

/* Access checking stubs */
int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf,
		      int len, unsigned int gup_flags)
{
	return 0;
}
EXPORT_SYMBOL(access_process_vm);

int __access_remote_vm(struct mm_struct *mm, unsigned long addr, void *buf,
		       int len, unsigned int gup_flags)
{
	return 0;
}

/* GUP stubs */
long get_user_pages(unsigned long start, unsigned long nr_pages,
		    unsigned int gup_flags, struct page **pages)
{
	return 0;
}
EXPORT_SYMBOL(get_user_pages);

long pin_user_pages(unsigned long start, unsigned long nr_pages,
		    unsigned int gup_flags, struct page **pages)
{
	return 0;
}
EXPORT_SYMBOL(pin_user_pages);

void unpin_user_pages(struct page **pages, unsigned long npages)
{
}
EXPORT_SYMBOL(unpin_user_pages);

int get_user_pages_fast(unsigned long start, int nr_pages,
			unsigned int gup_flags, struct page **pages)
{
	return 0;
}
EXPORT_SYMBOL(get_user_pages_fast);

/* MM struct operations */
int mm_take_all_locks(struct mm_struct *mm)
{
	return 0;
}

void mm_drop_all_locks(struct mm_struct *mm)
{
}

void exit_mmap(struct mm_struct *mm)
{
}

/* Page table stubs */
int __pte_alloc(struct mm_struct *mm, pmd_t *pmd)
{
	return 0;
}

int __pte_alloc_kernel(pmd_t *pmd)
{
	return 0;
}

/* Process memory stubs */
void dup_mm_exe_file(struct mm_struct *mm, struct mm_struct *oldmm)
{
}

/* RMAP stubs */
void page_add_anon_rmap(struct page *page, struct vm_area_struct *vma,
			unsigned long address, rmap_t flags)
{
}

void page_add_file_rmap(struct page *page, struct vm_area_struct *vma,
			bool compound)
{
}

void page_remove_rmap(struct page *page, struct vm_area_struct *vma,
		      bool compound)
{
}

void folio_add_file_rmap_range(struct folio *folio, struct page *page,
			       unsigned int nr_pages,
			       struct vm_area_struct *vma, bool compound)
{
}

bool vma_is_stack_for_current(struct vm_area_struct *vma)
{
	return false;
}

/* Split stub */
int split_vma(struct vma_iterator *vmi, struct vm_area_struct *vma,
	      unsigned long addr, int new_below)
{
	return -ENOMEM;
}

/* Process VM access syscalls */
SYSCALL_DEFINE6(process_vm_readv, pid_t, pid, const struct iovec __user *, lvec,
		unsigned long, liovcnt, const struct iovec __user *, rvec,
		unsigned long, riovcnt, unsigned long, flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE6(process_vm_writev, pid_t, pid, const struct iovec __user *,
		lvec, unsigned long, liovcnt, const struct iovec __user *, rvec,
		unsigned long, riovcnt, unsigned long, flags)
{
	return -ENOSYS;
}

/* mincore stub */
SYSCALL_DEFINE3(mincore, unsigned long, start, size_t, len,
		unsigned char __user *, vec)
{
	return -ENOSYS;
}

#ifdef CONFIG_X86_32
/* old_mmap for 32-bit compatibility */
struct mmap_arg_struct32 {
	u32 addr;
	u32 len;
	u32 prot;
	u32 flags;
	u32 fd;
	u32 offset;
};

SYSCALL_DEFINE1(old_mmap, struct mmap_arg_struct32 __user *, arg)
{
	return -ENOSYS;
}
#endif
