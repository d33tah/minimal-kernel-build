#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/security.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mman.h>
#include <linux/hugetlb.h>
#include <linux/vmalloc.h>
#include <linux/userfaultfd_k.h>
#include <linux/elf.h>
#include <linux/elf-randomize.h>
#include <linux/personality.h>
#include <linux/random.h>

#include <linux/sizes.h>
#include <linux/compat.h>

#include <linux/uaccess.h>

#include "internal.h"
#include "swap.h"

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

char *kstrndup(const char *s, size_t max, gfp_t gfp)
{
	size_t len;
	char *buf;

	if (!s)
		return NULL;

	len = strnlen(s, max);
	buf = kmalloc_track_caller(len+1, gfp);
	if (buf) {
		memcpy(buf, s, len);
		buf[len] = '\0';
	}
	return buf;
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

void *memdup_user(const void __user *src, size_t len)
{
	void *p;

	p = kmalloc_track_caller(len, GFP_USER | __GFP_NOWARN);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, src, len)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}

	return p;
}

/* Stub: vmemdup_user not used in minimal kernel */
void *vmemdup_user(const void __user *src, size_t len)
{
	return ERR_PTR(-ENOMEM);
}

char *strndup_user(const char __user *s, long n)
{
	char *p;
	long length;

	length = strnlen_user(s, n);

	if (!length)
		return ERR_PTR(-EFAULT);

	if (length > n)
		return ERR_PTR(-EINVAL);

	p = memdup_user(s, length);

	if (IS_ERR(p))
		return p;

	p[length - 1] = '\0';

	return p;
}

/* Stub: memdup_user_nul not used in minimal kernel */
void *memdup_user_nul(const void __user *src, size_t len)
{
	return ERR_PTR(-ENOMEM);
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

void __vma_unlink_list(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *prev, *next;

	next = vma->vm_next;
	prev = vma->vm_prev;
	if (prev)
		prev->vm_next = next;
	else
		mm->mmap = next;
	if (next)
		next->vm_prev = prev;
}

/* Stub: vma_is_stack_for_current not used in minimal kernel */
int vma_is_stack_for_current(struct vm_area_struct *vma) { return 0; }

void vma_set_file(struct vm_area_struct *vma, struct file *file)
{
	 
	get_file(file);
	swap(vma->vm_file, file);
	fput(file);
}

#ifndef STACK_RND_MASK
#define STACK_RND_MASK (0x7ff >> (PAGE_SHIFT - 12))      
#endif

unsigned long randomize_stack_top(unsigned long stack_top)
{
	unsigned long random_variable = 0;

	if (current->flags & PF_RANDOMIZE) {
		random_variable = get_random_long();
		random_variable &= STACK_RND_MASK;
		random_variable <<= PAGE_SHIFT;
	}
	return PAGE_ALIGN(stack_top) - random_variable;
}

unsigned long randomize_page(unsigned long start, unsigned long range)
{
	if (!PAGE_ALIGNED(start)) {
		range -= PAGE_ALIGN(start) - start;
		start = PAGE_ALIGN(start);
	}

	if (start > ULONG_MAX - range)
		range = ULONG_MAX - start;

	range >>= PAGE_SHIFT;

	if (range == 0)
		return start;

	return start + (get_random_long() % range << PAGE_SHIFT);
}

#if   defined(CONFIG_MMU) && !defined(HAVE_ARCH_PICK_MMAP_LAYOUT)
void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	mm->mmap_base = TASK_UNMAPPED_BASE;
	mm->get_unmapped_area = arch_get_unmapped_area;
}
#endif

/* Stub: __account_locked_vm not used in minimal kernel */
int __account_locked_vm(struct mm_struct *mm, unsigned long pages, bool inc,
			struct task_struct *task, bool bypass_rlim)
{
	return 0;
}

/* Stub: account_locked_vm not used in minimal kernel */
int account_locked_vm(struct mm_struct *mm, unsigned long pages, bool inc)
{
	return 0;
}

unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff)
{
	unsigned long ret;
	struct mm_struct *mm = current->mm;
	unsigned long populate;
	LIST_HEAD(uf);

	ret = security_mmap_file(file, prot, flag);
	if (!ret) {
		if (mmap_write_lock_killable(mm))
			return -EINTR;
		ret = do_mmap(file, addr, len, prot, flag, pgoff, &populate,
			      &uf);
		mmap_write_unlock(mm);
		userfaultfd_unmap_complete(mm, &uf);
		if (populate)
			mm_populate(ret, populate);
	}
	return ret;
}

unsigned long vm_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	if (unlikely(offset + PAGE_ALIGN(len) < offset))
		return -EINVAL;
	if (unlikely(offset_in_page(offset)))
		return -EINVAL;

	return vm_mmap_pgoff(file, addr, len, prot, flag, offset >> PAGE_SHIFT);
}

void *kvmalloc_node(size_t size, gfp_t flags, int node)
{
	gfp_t kmalloc_flags = flags;
	void *ret;

	 
	if (size > PAGE_SIZE) {
		kmalloc_flags |= __GFP_NOWARN;

		if (!(kmalloc_flags & __GFP_RETRY_MAYFAIL))
			kmalloc_flags |= __GFP_NORETRY;

		 
		kmalloc_flags &= ~__GFP_NOFAIL;
	}

	ret = kmalloc_node(size, kmalloc_flags, node);

	 
	if (ret || size <= PAGE_SIZE)
		return ret;

	 
	if (unlikely(size > INT_MAX)) {
		WARN_ON_ONCE(!(flags & __GFP_NOWARN));
		return NULL;
	}

	 
	return __vmalloc_node_range(size, 1, VMALLOC_START, VMALLOC_END,
			flags, PAGE_KERNEL, VM_ALLOW_HUGE_VMAP,
			node, __builtin_return_address(0));
}

void kvfree(const void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}

/* Stub: kvfree_sensitive not used in minimal kernel */
void kvfree_sensitive(const void *addr, size_t len) { }

/* Stub: kvrealloc not used in minimal kernel */
void *kvrealloc(const void *p, size_t oldsize, size_t newsize, gfp_t flags)
{
	return NULL;
}

/* Stub: __vmalloc_array not used in minimal kernel */
void *__vmalloc_array(size_t n, size_t size, gfp_t flags) { return NULL; }

/* Stub: vmalloc_array not used in minimal kernel */
void *vmalloc_array(size_t n, size_t size) { return NULL; }

/* Stub: __vcalloc not used in minimal kernel */
void *__vcalloc(size_t n, size_t size, gfp_t flags) { return NULL; }

/* Stub: vcalloc not used in minimal kernel */
void *vcalloc(size_t n, size_t size) { return NULL; }

void *page_rmapping(struct page *page)
{
	return folio_raw_mapping(page_folio(page));
}

bool folio_mapped(struct folio *folio)
{
	long i, nr;

	if (!folio_test_large(folio))
		return atomic_read(&folio->_mapcount) >= 0;
	if (atomic_read(folio_mapcount_ptr(folio)) >= 0)
		return true;
	if (folio_test_hugetlb(folio))
		return false;

	nr = folio_nr_pages(folio);
	for (i = 0; i < nr; i++) {
		if (atomic_read(&folio_page(folio, i)->_mapcount) >= 0)
			return true;
	}
	return false;
}

struct anon_vma *folio_anon_vma(struct folio *folio)
{
	unsigned long mapping = (unsigned long)folio->mapping;

	if ((mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_ANON)
		return NULL;
	return (void *)(mapping - PAGE_MAPPING_ANON);
}

struct address_space *folio_mapping(struct folio *folio)
{
	struct address_space *mapping;

	 
	if (unlikely(folio_test_slab(folio)))
		return NULL;

	if (unlikely(folio_test_swapcache(folio)))
		return swap_address_space(folio_swap_entry(folio));

	mapping = folio->mapping;
	if ((unsigned long)mapping & PAGE_MAPPING_ANON)
		return NULL;

	return (void *)((unsigned long)mapping & ~PAGE_MAPPING_FLAGS);
}

int __page_mapcount(struct page *page)
{
	int ret;

	ret = atomic_read(&page->_mapcount) + 1;
	 
	if (!PageAnon(page) && !PageHuge(page))
		return ret;
	page = compound_head(page);
	ret += atomic_read(compound_mapcount_ptr(page)) + 1;
	if (PageDoubleMap(page))
		ret--;
	return ret;
}

int folio_mapcount(struct folio *folio)
{
	int i, compound, nr, ret;

	if (likely(!folio_test_large(folio)))
		return atomic_read(&folio->_mapcount) + 1;

	compound = folio_entire_mapcount(folio);
	nr = folio_nr_pages(folio);
	if (folio_test_hugetlb(folio))
		return compound;
	ret = compound;
	for (i = 0; i < nr; i++)
		ret += atomic_read(&folio_page(folio, i)->_mapcount) + 1;
	 
	if (!folio_test_anon(folio))
		return ret - compound * nr;
	if (folio_test_double_map(folio))
		ret -= nr;
	return ret;
}

/* Stub: folio_copy not used in minimal kernel */
void folio_copy(struct folio *dst, struct folio *src)
{
}

int sysctl_overcommit_memory __read_mostly = OVERCOMMIT_GUESS;
int sysctl_overcommit_ratio __read_mostly = 50;
unsigned long sysctl_overcommit_kbytes __read_mostly;
int sysctl_max_map_count __read_mostly = DEFAULT_MAX_MAP_COUNT;
unsigned long sysctl_user_reserve_kbytes __read_mostly = 1UL << 17;  
unsigned long sysctl_admin_reserve_kbytes __read_mostly = 1UL << 13;  

/* Stub: overcommit handlers not used in minimal kernel */
int overcommit_ratio_handler(struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos) { return 0; }
int overcommit_policy_handler(struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos) { return 0; }
int overcommit_kbytes_handler(struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos) { return 0; }

unsigned long vm_commit_limit(void)
{
	unsigned long allowed;

	if (sysctl_overcommit_kbytes)
		allowed = sysctl_overcommit_kbytes >> (PAGE_SHIFT - 10);
	else
		allowed = ((totalram_pages() - hugetlb_total_pages())
			   * sysctl_overcommit_ratio / 100);
	allowed += total_swap_pages;

	return allowed;
}

struct percpu_counter vm_committed_as ____cacheline_aligned_in_smp;

/* Stub: vm_memory_committed not used in minimal kernel */
unsigned long vm_memory_committed(void) { return 0; }

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

/* Stub: get_cmdline not used in minimal kernel */
int get_cmdline(struct task_struct *task, char *buffer, int buflen) { return 0; }

int __weak memcmp_pages(struct page *page1, struct page *page2)
{
	char *addr1, *addr2;
	int ret;

	addr1 = kmap_atomic(page1);
	addr2 = kmap_atomic(page2);
	ret = memcmp(addr1, addr2, PAGE_SIZE);
	kunmap_atomic(addr2);
	kunmap_atomic(addr1);
	return ret;
}


/* Stub: page_offline functions not needed for minimal kernel without memory hotplug */
void page_offline_freeze(void) { }
void page_offline_thaw(void) { }
void page_offline_begin(void) { }
void page_offline_end(void) { }

#ifndef ARCH_IMPLEMENTS_FLUSH_DCACHE_FOLIO
void flush_dcache_folio(struct folio *folio)
{
	long i, nr = folio_nr_pages(folio);

	for (i = 0; i < nr; i++)
		flush_dcache_page(folio_page(folio, i));
}
#endif
