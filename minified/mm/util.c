#include <asm/sections.h>
#include <linux/swap.h>
#include <linux/mman.h>

void kfree_const(const void *x)
{
	if (!is_kernel_rodata((unsigned long)x))
		kfree(x);
}

static char *kstrdup(const char *s, gfp_t gfp)
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
	LIST_HEAD(uf);

	if (mmap_write_lock_killable(mm))
		return -EINTR;
	ret = do_mmap(file, addr, len, prot, flag, pgoff, NULL, &uf);
	mmap_write_unlock(mm);
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

struct percpu_counter vm_committed_as ____cacheline_aligned_in_smp;

/* Simplified: always OVERCOMMIT_GUESS mode */
int __vm_enough_memory(struct mm_struct *mm, long pages, int cap_sys_admin)
{
	vm_acct_memory(pages);

	if (pages > totalram_pages() + total_swap_pages)
		goto error;
	return 0;

error:
	vm_unacct_memory(pages);
	return -ENOMEM;
}
