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
#include <linux/mman.h>
#include <linux/vmalloc.h>
#include <linux/personality.h>
#include <linux/uaccess.h>

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

static unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
				   unsigned long len, unsigned long prot,
				   unsigned long flag, unsigned long pgoff)
{
	unsigned long ret;
	struct mm_struct *mm = current->mm;
	unsigned long populate;
	LIST_HEAD(uf);

	if (mmap_write_lock_killable(mm))
		return -EINTR;
	ret = do_mmap(file, addr, len, prot, flag, pgoff, &populate, &uf);
	mmap_write_unlock(mm);
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
}

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
