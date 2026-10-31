#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/swap.h>
#include <linux/random.h>

#include "internal.h"

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

unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
			    unsigned long len, unsigned long prot,
			    unsigned long flag, unsigned long pgoff)
{
	unsigned long ret;
	struct mm_struct *mm = current->mm;
	unsigned long populate;

	if (mmap_write_lock_killable(mm))
		return -EINTR;
	ret = do_mmap(file, addr, len, prot, flag, pgoff, &populate);
	mmap_write_unlock(mm);
	/* populate is always 0 here (VM_LOCKED/MAP_POPULATE never set). */
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

int sysctl_max_map_count __read_mostly = DEFAULT_MAX_MAP_COUNT;

/*
 * __vm_enough_memory only ever runs the simple totalram/swap guard on this
 * build (no overcommit sysctl writer; the OVERCOMMIT_ALWAYS short-circuit and
 * the OVERCOMMIT_NEVER commit-limit tail were unreachable and are gone).
 */
int __vm_enough_memory(struct mm_struct *mm, long pages, int cap_sys_admin)
{
	if (pages > totalram_pages() + total_swap_pages)
		return -ENOMEM;

	return 0;
}
