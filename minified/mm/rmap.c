
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rmap.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/memcontrol.h>
#include <linux/hugetlb.h>
#include <linux/huge_mm.h>
#include <linux/backing-dev.h>
#include <linux/memremap.h>
#include <linux/userfaultfd_k.h>
#include <linux/mm_inline.h>

#include <asm/tlbflush.h>

#include "internal.h"

static struct kmem_cache *anon_vma_cachep;
static struct kmem_cache *anon_vma_chain_cachep;

static inline struct anon_vma *anon_vma_alloc(void)
{
	struct anon_vma *anon_vma;

	anon_vma = kmem_cache_alloc(anon_vma_cachep, GFP_KERNEL);
	if (anon_vma) {
		atomic_set(&anon_vma->refcount, 1);
		anon_vma->degree = 1;	
		anon_vma->parent = anon_vma;
		
		anon_vma->root = anon_vma;
	}

	return anon_vma;
}

static inline void anon_vma_free(struct anon_vma *anon_vma)
{
	VM_BUG_ON(atomic_read(&anon_vma->refcount));

	might_sleep();
	if (rwsem_is_locked(&anon_vma->root->rwsem)) {
		anon_vma_lock_write(anon_vma);
		anon_vma_unlock_write(anon_vma);
	}

	kmem_cache_free(anon_vma_cachep, anon_vma);
}

static inline struct anon_vma_chain *anon_vma_chain_alloc(gfp_t gfp)
{
	return kmem_cache_alloc(anon_vma_chain_cachep, gfp);
}

static void anon_vma_chain_free(struct anon_vma_chain *anon_vma_chain)
{
	kmem_cache_free(anon_vma_chain_cachep, anon_vma_chain);
}

static void anon_vma_chain_link(struct vm_area_struct *vma,
				struct anon_vma_chain *avc,
				struct anon_vma *anon_vma)
{
	avc->vma = vma;
	avc->anon_vma = anon_vma;
	list_add(&avc->same_vma, &vma->anon_vma_chain);
	anon_vma_interval_tree_insert(avc, &anon_vma->rb_root);
}

int __anon_vma_prepare(struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	struct anon_vma *anon_vma, *allocated;
	struct anon_vma_chain *avc;

	might_sleep();

	avc = anon_vma_chain_alloc(GFP_KERNEL);
	if (!avc)
		goto out_enomem;

	anon_vma = find_mergeable_anon_vma(vma);
	allocated = NULL;
	if (!anon_vma) {
		anon_vma = anon_vma_alloc();
		if (unlikely(!anon_vma))
			goto out_enomem_free_avc;
		allocated = anon_vma;
	}

	anon_vma_lock_write(anon_vma);
	
	spin_lock(&mm->page_table_lock);
	if (likely(!vma->anon_vma)) {
		vma->anon_vma = anon_vma;
		anon_vma_chain_link(vma, avc, anon_vma);
		
		anon_vma->degree++;
		allocated = NULL;
		avc = NULL;
	}
	spin_unlock(&mm->page_table_lock);
	anon_vma_unlock_write(anon_vma);

	if (unlikely(allocated))
		put_anon_vma(allocated);
	if (unlikely(avc))
		anon_vma_chain_free(avc);

	return 0;

 out_enomem_free_avc:
	anon_vma_chain_free(avc);
 out_enomem:
	return -ENOMEM;
}

static inline struct anon_vma *lock_anon_vma_root(struct anon_vma *root, struct anon_vma *anon_vma)
{
	struct anon_vma *new_root = anon_vma->root;
	if (new_root != root) {
		if (WARN_ON_ONCE(root))
			up_write(&root->rwsem);
		root = new_root;
		down_write(&root->rwsem);
	}
	return root;
}

static inline void unlock_anon_vma_root(struct anon_vma *root)
{
	if (root)
		up_write(&root->rwsem);
}

int anon_vma_clone(struct vm_area_struct *dst, struct vm_area_struct *src)
{
	struct anon_vma_chain *avc, *pavc;
	struct anon_vma *root = NULL;

	list_for_each_entry_reverse(pavc, &src->anon_vma_chain, same_vma) {
		struct anon_vma *anon_vma;

		avc = anon_vma_chain_alloc(GFP_NOWAIT | __GFP_NOWARN);
		if (unlikely(!avc)) {
			unlock_anon_vma_root(root);
			root = NULL;
			avc = anon_vma_chain_alloc(GFP_KERNEL);
			if (!avc)
				goto enomem_failure;
		}
		anon_vma = pavc->anon_vma;
		root = lock_anon_vma_root(root, anon_vma);
		anon_vma_chain_link(dst, avc, anon_vma);

		if (!dst->anon_vma && src->anon_vma &&
		    anon_vma != src->anon_vma && anon_vma->degree < 2)
			dst->anon_vma = anon_vma;
	}
	if (dst->anon_vma)
		dst->anon_vma->degree++;
	unlock_anon_vma_root(root);
	return 0;

 enomem_failure:
	
	dst->anon_vma = NULL;
	unlink_anon_vmas(dst);
	return -ENOMEM;
}

void unlink_anon_vmas(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc, *next;
	struct anon_vma *root = NULL;

	list_for_each_entry_safe(avc, next, &vma->anon_vma_chain, same_vma) {
		struct anon_vma *anon_vma = avc->anon_vma;

		root = lock_anon_vma_root(root, anon_vma);
		anon_vma_interval_tree_remove(avc, &anon_vma->rb_root);

		if (RB_EMPTY_ROOT(&anon_vma->rb_root.rb_root)) {
			anon_vma->parent->degree--;
			continue;
		}

		list_del(&avc->same_vma);
		anon_vma_chain_free(avc);
	}
	if (vma->anon_vma) {
		vma->anon_vma->degree--;

		vma->anon_vma = NULL;
	}
	unlock_anon_vma_root(root);

	list_for_each_entry_safe(avc, next, &vma->anon_vma_chain, same_vma) {
		struct anon_vma *anon_vma = avc->anon_vma;

		VM_WARN_ON(anon_vma->degree);
		put_anon_vma(anon_vma);

		list_del(&avc->same_vma);
		anon_vma_chain_free(avc);
	}
}

static void anon_vma_ctor(void *data)
{
	struct anon_vma *anon_vma = data;

	init_rwsem(&anon_vma->rwsem);
	atomic_set(&anon_vma->refcount, 0);
	anon_vma->rb_root = RB_ROOT_CACHED;
}

void __init anon_vma_init(void)
{
	anon_vma_cachep = kmem_cache_create("anon_vma", sizeof(struct anon_vma),
			0, SLAB_TYPESAFE_BY_RCU|SLAB_PANIC|SLAB_ACCOUNT,
			anon_vma_ctor);
	anon_vma_chain_cachep = KMEM_CACHE(anon_vma_chain,
			SLAB_PANIC|SLAB_ACCOUNT);
}


#define TLB_FLUSH_BATCH_FLUSHED_SHIFT	16
#define TLB_FLUSH_BATCH_PENDING_MASK			\
	((1 << (TLB_FLUSH_BATCH_FLUSHED_SHIFT - 1)) - 1)

void flush_tlb_batched_pending(struct mm_struct *mm)
{
	int batch = atomic_read(&mm->tlb_flush_batched);
	int pending = batch & TLB_FLUSH_BATCH_PENDING_MASK;
	int flushed = batch >> TLB_FLUSH_BATCH_FLUSHED_SHIFT;

	if (pending != flushed) {
		flush_tlb_mm(mm);
		
		atomic_cmpxchg(&mm->tlb_flush_batched, batch,
			       pending | (pending << TLB_FLUSH_BATCH_FLUSHED_SHIFT));
	}
}


/* Stubbed: folio_mkclean used by truncate.c */
int folio_mkclean(struct folio *folio) { return 0; }

static void __page_set_anon_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address, int exclusive)
{
	struct anon_vma *anon_vma = vma->anon_vma;

	BUG_ON(!anon_vma);

	if (PageAnon(page))
		goto out;

	if (!exclusive)
		anon_vma = anon_vma->root;

	anon_vma = (void *) anon_vma + PAGE_MAPPING_ANON;
	WRITE_ONCE(page->mapping, (struct address_space *) anon_vma);
	page->index = linear_page_index(vma, address);
out:
	if (exclusive)
		SetPageAnonExclusive(page);
}

void page_add_new_anon_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address)
{
	const bool compound = PageCompound(page);
	int nr = compound ? thp_nr_pages(page) : 1;

	VM_BUG_ON_VMA(address < vma->vm_start || address >= vma->vm_end, vma);
	__SetPageSwapBacked(page);
	if (compound) {
		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
		
		atomic_set(compound_mapcount_ptr(page), 0);
		atomic_set(compound_pincount_ptr(page), 0);

		__mod_lruvec_page_state(page, NR_ANON_THPS, nr);
	} else {
		
		atomic_set(&page->_mapcount, 0);
	}
	__mod_lruvec_page_state(page, NR_ANON_MAPPED, nr);
	__page_set_anon_rmap(page, vma, address, 1);
}

void page_add_file_rmap(struct page *page,
	struct vm_area_struct *vma, bool compound)
{
	int nr = 0;

	if (atomic_inc_and_test(&page->_mapcount))
		nr++;
	if (nr)
		__mod_lruvec_page_state(page, NR_FILE_MAPPED, nr);

	mlock_vma_page(page, vma, compound);
}

void __put_anon_vma(struct anon_vma *anon_vma)
{
	struct anon_vma *root = anon_vma->root;

	anon_vma_free(anon_vma);
	if (root != anon_vma && atomic_dec_and_test(&root->refcount))
		anon_vma_free(root);
}

