#include <linux/mm.h>
#include <asm/sections.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/security.h>
#include <linux/swap.h>
/* swapops.h removed - was empty */
#include <linux/mman.h>
/* linux/hugetlb.h removed - hugetlb stubs in header */
#include <linux/vmalloc.h>
/* userfaultfd_k.h, linux/elf.h removed - not used */
#include <linux/personality.h>
/* Removed: linux/random.h - randomization disabled */
/* linux/compat.h removed - no compat functions used */
#include <linux/uaccess.h>

#include "internal.h"
/* struct swap_iocb forward decl removed - unused */

void kfree_const(const void *x)
{
	if (!is_kernel_rodata((unsigned long)x))
		kfree(x);
}

char *kstrdup(const char *s, gfp_t gfp)
{
	size_t len;
	char *buf;

	if (!s)
		return NULL;

	len = strlen(s) + 1;
	buf = kmalloc_track_caller(len, gfp);
	if (buf)
		memcpy(buf, s, len);
	return buf;
}

const char *kstrdup_const(const char *s, gfp_t gfp)
{
	if (is_kernel_rodata((unsigned long)s))
		return s;

	return kstrdup(s, gfp);
}

void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *p;

	p = kmalloc_track_caller(len, gfp);
	if (p)
		memcpy(p, src, len);
	return p;
}

char *kmemdup_nul(const char *s, size_t len, gfp_t gfp)
{
	char *buf;

	if (!s)
		return NULL;

	buf = kmalloc_track_caller(len + 1, gfp);
	if (buf) {
		memcpy(buf, s, len);
		buf[len] = '\0';
	}
	return buf;
}
/* memdup_user removed - never called */

void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		     struct vm_area_struct *prev)
{
	struct vm_area_struct *next;

	vma->vm_prev = prev;
	if (prev) {
		next = prev->vm_next;
		prev->vm_next = vma;
	} else {
		next = mm->mmap;
		mm->mmap = vma;
	}
	vma->vm_next = next;
	if (next)
		next->vm_prev = vma;
}

/* __vma_unlink_list removed - never called */

/* randomize_stack_top, randomize_page removed - inlined to PAGE_ALIGN (~4 LOC) */

/* arch_pick_mmap_layout removed - HAVE_ARCH_PICK_MMAP_LAYOUT is defined on x86 */

static unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
				   unsigned long len, unsigned long prot,
				   unsigned long flag, unsigned long pgoff)
{
	unsigned long ret;
	struct mm_struct *mm = current->mm;
	unsigned long populate;
	LIST_HEAD(uf);

	/* security_mmap_file always returns 0 */
	if (mmap_write_lock_killable(mm))
		return -EINTR;
	ret = do_mmap(file, addr, len, prot, flag, pgoff, &populate, &uf);
	mmap_write_unlock(mm);
	/* userfaultfd_unmap_complete call removed - empty stub */
	if (populate)
		mm_populate(ret, populate);
	return ret;
}

unsigned long vm_mmap(struct file *file, unsigned long addr, unsigned long len,
		      unsigned long prot, unsigned long flag,
		      unsigned long offset)
{
	if (unlikely(offset + PAGE_ALIGN(len) < offset))
		return -EINVAL;
	if (unlikely(offset_in_page(offset)))
		return -EINVAL;

	return vm_mmap_pgoff(file, addr, len, prot, flag, offset >> PAGE_SHIFT);
}

void kvfree(const void *addr)
{
	/* Both vfree and kfree are empty stubs - nothing to do */
}

/* folio_mapped, __page_mapcount removed - no callers */

int sysctl_overcommit_memory __read_mostly = OVERCOMMIT_GUESS;
int sysctl_overcommit_ratio __read_mostly = 50;
unsigned long sysctl_overcommit_kbytes __read_mostly;
int sysctl_max_map_count __read_mostly = DEFAULT_MAX_MAP_COUNT;
unsigned long sysctl_user_reserve_kbytes __read_mostly = 1UL << 17;
unsigned long sysctl_admin_reserve_kbytes __read_mostly = 1UL << 13;

static unsigned long vm_commit_limit(void)
{
	unsigned long allowed;

	if (sysctl_overcommit_kbytes)
		allowed = sysctl_overcommit_kbytes >> (PAGE_SHIFT - 10);
	else
		/* hugetlb_total_pages() always returns 0 */
		allowed = (totalram_pages() * sysctl_overcommit_ratio / 100);
	allowed += total_swap_pages;

	return allowed;
}

struct percpu_counter vm_committed_as ____cacheline_aligned_in_smp;

int __vm_enough_memory(struct mm_struct *mm, long pages, int cap_sys_admin)
{
	long allowed;

	vm_acct_memory(pages);

	if (sysctl_overcommit_memory == OVERCOMMIT_ALWAYS)
		return 0;

	if (sysctl_overcommit_memory == OVERCOMMIT_GUESS) {
		if (pages > totalram_pages() + total_swap_pages)
			goto error;
		return 0;
	}

	allowed = vm_commit_limit();

	if (!cap_sys_admin)
		allowed -= sysctl_admin_reserve_kbytes >> (PAGE_SHIFT - 10);

	if (mm) {
		long reserve = sysctl_user_reserve_kbytes >> (PAGE_SHIFT - 10);

		allowed -= min_t(long, mm->total_vm / 32, reserve);
	}

	if (percpu_counter_read_positive(&vm_committed_as) < allowed)
		return 0;
error:
	vm_unacct_memory(pages);

	return -ENOMEM;
}

/* flush_dcache_folio - already defined as empty stub in highmem.h */
