/* Simplified rmap - no chain tracking needed for hello-world */

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
	init_rwsem(&anon_vma->rwsem);
	vma->anon_vma = anon_vma;
	return 0;
}

void __init anon_vma_init(void)
{
	anon_vma_cachep = kmem_cache_create(
		"anon_vma", sizeof(struct anon_vma), 0,
		SLAB_TYPESAFE_BY_RCU | SLAB_PANIC | SLAB_ACCOUNT, NULL);
}

void page_add_new_anon_rmap(struct page *page, struct vm_area_struct *vma,
			    unsigned long address)
{
	atomic_set(&page->_mapcount, 0);
	WRITE_ONCE(page->mapping,
		   (struct address_space *)((void *)vma->anon_vma +
					    PAGE_MAPPING_ANON));
	page->index = linear_page_index(vma, address);
}
