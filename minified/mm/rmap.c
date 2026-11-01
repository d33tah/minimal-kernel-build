/*
 * mm/rmap.c - physical to virtual reverse mappings
 *
 * Copyright 2001, Rik van Riel <riel@conectiva.com.br>
 * Released under the General Public License (GPL).
 *
 * Simple, low overhead reverse mapping scheme.
 * Please try to keep this thing as modular as possible.
 *
 * Provides methods for unmapping each kind of mapped page:
 * the anon methods track anonymous pages, and
 * the file methods track pages belonging to an inode.
 *
 * Original design by Rik van Riel <riel@conectiva.com.br> 2001
 * File methods by Dave McCracken <dmccr@us.ibm.com> 2003, 2004
 * Anonymous methods by Andrea Arcangeli <andrea@suse.de> 2004
 * Contributions by Hugh Dickins 2003, 2004
 */

/*
 * Lock ordering in mm:
 *
 * inode->i_rwsem	(while writing or truncating, not reading or faulting)
 *   mm->mmap_lock
 *     mapping->invalidate_lock (in filemap_fault)
 *       page->flags PG_locked (lock_page)   * (see hugetlbfs below)
 *         hugetlbfs_i_mmap_rwsem_key (in huge_pmd_share)
 *           mapping->i_mmap_rwsem
 *             hugetlb_fault_mutex (hugetlbfs specific page fault mutex)
 *             anon_vma->rwsem
 *               mm->page_table_lock or pte_lock
 *                 swap_lock (in swap_duplicate, swap_info_get)
 *                   mmlist_lock (in mmput, drain_mmlist and others)
 *                   mapping->private_lock (in block_dirty_folio)
 *                     folio_lock_memcg move_lock (in block_dirty_folio)
 *                       i_pages lock (widely used)
 *                         lruvec->lru_lock (in folio_lruvec_lock_irq)
 *                   inode->i_lock (in set_page_dirty's __mark_inode_dirty)
 *                   bdi.wb->list_lock (in set_page_dirty's __mark_inode_dirty)
 *                     sb_lock (within inode_lock in fs/fs-writeback.c)
 *                     i_pages lock (widely used, in set_page_dirty,
 *                               in arch-dependent flush_dcache_mmap_lock,
 *                               within bdi.wb->list_lock in __sync_single_inode)
 *
 * anon_vma->rwsem,mapping->i_mmap_rwsem   (memory_failure, collect_procs_anon)
 *   ->tasklist_lock
 *     pte map lock
 *
 * * hugetlbfs PageHuge() pages take locks in this order:
 *         mapping->i_mmap_rwsem
 *           hugetlb_fault_mutex (hugetlbfs specific page fault mutex)
 *             page->flags PG_locked (lock_page)
 */

#include "internal.h"

/*
 * No reverse mapping: nothing in this kernel walks from a page back to the
 * page tables that map it (no reclaim, migration or truncation of mapped
 * pages), so there is no anon_vma and no i_mmap tree. A new anonymous page
 * only records that it is anonymous and its mapcount.
 */
void page_add_new_anon_rmap(struct page *page, struct vm_area_struct *vma,
			    unsigned long address)
{
	const bool compound = PageCompound(page);

	if (compound) {
		/* increment count (starts at -1) */
		atomic_set(compound_mapcount_ptr(page), 0);
		atomic_set(compound_pincount_ptr(page), 0);
	} else {
		/* increment count (starts at -1) */
		atomic_set(&page->_mapcount, 0);
	}
	WRITE_ONCE(page->mapping, (struct address_space *)PAGE_MAPPING_ANON);
	page->index = linear_page_index(vma, address);
}

void page_add_file_rmap(struct page *page, struct vm_area_struct *vma,
			bool compound)
{
	atomic_inc(&page->_mapcount);
}

/* The page lost one of its PTEs (munmap). */
void page_remove_rmap(struct page *page)
{
	atomic_dec(&page->_mapcount);
}
