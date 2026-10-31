
#include "internal.h"

/*
 * No reverse mapping: nothing in this kernel walks from a page back to the
 * page tables that map it (no reclaim, migration or truncation of mapped
 * pages), so there is no anon_vma and no i_mmap tree. A new anonymous page
 * only records that it is anonymous, exclusive to its mm, and its mapcount.
 */
void page_add_new_anon_rmap(struct page *page, struct vm_area_struct *vma,
			    unsigned long address)
{
	const bool compound = PageCompound(page);

	VM_BUG_ON_VMA(address < vma->vm_start || address >= vma->vm_end, vma);
	__SetPageSwapBacked(page);
	if (compound) {
		atomic_set(compound_mapcount_ptr(page), 0);
		atomic_set(compound_pincount_ptr(page), 0);
	} else {
		atomic_set(&page->_mapcount, 0);
	}
	WRITE_ONCE(page->mapping, (struct address_space *)PAGE_MAPPING_ANON);
	page->index = linear_page_index(vma, address);
	SetPageAnonExclusive(page);
}

void page_add_file_rmap(struct page *page, struct vm_area_struct *vma,
			bool compound)
{
	atomic_inc(&page->_mapcount);
}

/* The page lost one of its PTEs (munmap, COW replacement). */
void page_remove_rmap(struct page *page)
{
	atomic_dec(&page->_mapcount);
}
