/* Simplified rmap - no chain tracking or interval trees needed for hello-world */

#include "internal.h"

static struct kmem_cache *anon_vma_cachep;

int __anon_vma_prepare(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma;

	if (vma->anon_vma)
		return 0;

	anon_vma = kmem_cache_alloc(anon_vma_cachep, GFP_KERNEL);
	if (!anon_vma)
		return -ENOMEM;
	atomic_set(&anon_vma->refcount, 1);
	anon_vma->root = anon_vma;
	anon_vma->parent = anon_vma;
	anon_vma->degree = 1;
	vma->anon_vma = anon_vma;
	return 0;
}

void unlink_anon_vmas(struct vm_area_struct *vma)
{
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
	anon_vma_cachep = kmem_cache_create(
		"anon_vma", sizeof(struct anon_vma), 0,
		SLAB_TYPESAFE_BY_RCU | SLAB_PANIC | SLAB_ACCOUNT,
		anon_vma_ctor);
}

void page_add_new_anon_rmap(struct page *page, struct vm_area_struct *vma,
			    unsigned long address)
{
	struct anon_vma *anon_vma = vma->anon_vma;

	BUG_ON(!anon_vma);
	__SetPageSwapBacked(page);
	atomic_set(&page->_mapcount, 0);
	__mod_lruvec_page_state(page, NR_ANON_MAPPED, 1);
	WRITE_ONCE(page->mapping, (struct address_space *)((void *)anon_vma +
							   PAGE_MAPPING_ANON));
	page->index = linear_page_index(vma, address);
	SetPageAnonExclusive(page);
}
