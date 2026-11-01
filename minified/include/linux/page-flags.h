/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Macros for manipulating and testing page->flags
 */

#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

#ifndef __GENERATING_BOUNDS_H
#include <linux/mm_types.h>
#include <generated/bounds.h>
#endif /* !__GENERATING_BOUNDS_H */

/*
 * Various page->flags bits:
 *
 * PG_reserved is set for special pages. The "struct page" of such a page
 * should in general not be touched (e.g. set dirty) except by its owner.
 * Pages marked as PG_reserved include:
 * - Pages part of the kernel image (including vDSO) and similar (e.g. BIOS,
 *   initrd, HW tables)
 * - Pages reserved or allocated early during boot (before the page allocator
 *   was initialized). This includes (depending on the architecture) the
 *   initial vmemmap, initial page tables, crashkernel, elfcorehdr, and much
 *   much more. Once (if ever) freed, PG_reserved is cleared and they will
 *   be given to the page allocator.
 * - Pages falling into physical memory gaps - not IORESOURCE_SYSRAM. Trying
 *   to read/write these pages might end badly. Don't touch!
 * - The zero page(s)
 * - Pages not added to the page allocator when onlining a section because
 *   they were excluded via the online_page_callback() or because they are
 *   PG_hwpoison.
 * - Pages allocated in the context of kexec/kdump (loaded kernel image,
 *   control pages, vmcoreinfo)
 * - MMIO/DMA pages. Some architectures don't allow to ioremap pages that are
 *   not marked PG_reserved (as they might be in use by somebody else who does
 *   not respect the caching strategy).
 * - Pages part of an offline section (struct pages of offline sections should
 *   not be trusted as they will be initialized when first onlined).
 * - MCA pages on ia64
 * - Pages holding CPU notes for POWER Firmware Assisted Dump
 * - Device memory (e.g. PMEM, DAX, HMM)
 * Some PG_reserved pages will be excluded from the hibernation image.
 * PG_reserved does in general not hinder anybody from dumping or swapping
 * and is no longer required for remap_pfn_range(). ioremap might require it.
 * Consequently, PG_reserved for a page mapped into user space can indicate
 * the zero page, the vDSO, MMIO pages or device memory.
 *
 * The PG_private bitflag is set on pagecache pages if they contain filesystem
 * specific data (which is normally at page->private). It can be used by
 * private allocations for its own usage.
 *
 * During initiation of disk I/O, PG_locked is set. This bit is set before I/O
 * and cleared when writeback _starts_ or when read _completes_. PG_writeback
 * is set before writeback starts and cleared when it finishes.
 *
 * PG_locked also pins a page in pagecache, and blocks truncation of the file
 * while it is held.
 *
 * page_waitqueue(page) is a wait queue of all tasks waiting for the page
 * to become unlocked.
 *
 * PG_swapbacked is set when a page uses swap as a backing storage.  This are
 * usually PageAnon or shmem pages but please note that even anonymous pages
 * might lose their PG_swapbacked flag when they simply can be dropped (e.g. as
 * a result of MADV_FREE).
 *
 * PG_referenced, PG_reclaim are used for page reclaim for anonymous and
 * file-backed pagecache (see mm/vmscan.c).
 *
 * PG_error is set to indicate that an I/O error occurred on this page.
 *
 * PG_arch_1 is an architecture specific page state bit.  The generic code
 * guarantees that this bit is cleared for a page when it first is entered into
 * the page cache.
 *
 * PG_hwpoison indicates that a page got corrupted in hardware and contains
 * data with incorrect ECC bits that triggered a machine check. Accessing is
 * not safe since it may cause another machine check. Don't touch!
 */

/*
 * Don't use the pageflags directly.  Use the PageFoo macros.
 *
 * The page flags field is split into two parts, the main flags area
 * which extends from the low bits upwards, and the fields area which
 * extends from the high bits downwards.
 *
 *  | FIELD | ... | FLAGS |
 *  N-1           ^       0
 *               (NR_PAGEFLAGS)
 *
 * The fields area is reserved for fields mapping zone, node (for NUMA) and
 * SPARSEMEM section (for variants of SPARSEMEM that require section ids like
 * SPARSEMEM_EXTREME with !SPARSEMEM_VMEMMAP).
 */
enum pageflags {
	PG_locked, /* Page is locked. Don't touch. */
	PG_uptodate,
	PG_slab,
	PG_reserved,
	PG_head, /* A head page */
	__NR_PAGEFLAGS,
};

#define PAGEFLAGS_MASK ((1UL << NR_PAGEFLAGS) - 1)

#ifndef __GENERATING_BOUNDS_H

static inline const struct page *page_fixed_fake_head(const struct page *page)
{
	return page;
}

static __always_inline int page_is_fake_head(struct page *page)
{
	return page_fixed_fake_head(page) != page;
}

static inline unsigned long _compound_head(const struct page *page)
{
	unsigned long head = READ_ONCE(page->compound_head);

	if (unlikely(head & 1))
		return head - 1;
	return (unsigned long)page_fixed_fake_head(page);
}

#define compound_head(page) ((typeof(page))_compound_head(page))

#define page_folio(p)                                                          \
	(_Generic((p),                                                         \
		 const struct page *: (const struct folio *)_compound_head(p), \
		 struct page *: (struct folio *)_compound_head(p)))

/**
 * folio_page - Return a page from a folio.
 * @folio: The folio.
 * @n: The page number to return.
 *
 * @n is relative to the start of the folio.  This function does not
 * check that the page number lies within @folio; the caller is presumed
 * to have a reference to the page.
 */
#define folio_page(folio, n) nth_page(&(folio)->page, n)

static __always_inline int PageCompound(struct page *page)
{
	return test_bit(PG_head, &page->flags) ||
	       READ_ONCE(page->compound_head) & 1;
}

static unsigned long *folio_flags(struct folio *folio, unsigned n)
{
	struct page *page = &folio->page;

	return &page[n].flags;
}

#define PF_POISONED_CHECK(page) ({ page; })
#define PF_ANY(page, enforce) PF_POISONED_CHECK(page)
#define PF_NO_TAIL(page, enforce) ({ PF_POISONED_CHECK(compound_head(page)); })
#define PF_NO_COMPOUND(page, enforce) ({ PF_POISONED_CHECK(page); })

/* Which page is the flag stored in */
#define FOLIO_PF_ANY 0
#define FOLIO_PF_NO_TAIL 0
#define FOLIO_PF_NO_COMPOUND 0

/*
 * Macros to create function definitions for page flags
 */
#define TESTPAGEFLAG(uname, lname, policy)                                  \
	static __always_inline bool folio_test_##lname(struct folio *folio) \
	{                                                                   \
		return test_bit(PG_##lname,                                 \
				folio_flags(folio, FOLIO_##policy));        \
	}                                                                   \
	static __always_inline int Page##uname(struct page *page)           \
	{                                                                   \
		return test_bit(PG_##lname, &policy(page, 0)->flags);       \
	}

#define SETPAGEFLAG(uname, lname, policy)                                  \
	static __always_inline void folio_set_##lname(struct folio *folio) \
	{                                                                  \
		set_bit(PG_##lname, folio_flags(folio, FOLIO_##policy));   \
	}                                                                  \
	static __always_inline void SetPage##uname(struct page *page)      \
	{                                                                  \
		set_bit(PG_##lname, &policy(page, 1)->flags);              \
	}

#define CLEARPAGEFLAG(uname, lname, policy)                                  \
	static __always_inline void folio_clear_##lname(struct folio *folio) \
	{                                                                    \
		clear_bit(PG_##lname, folio_flags(folio, FOLIO_##policy));   \
	}                                                                    \
	static __always_inline void ClearPage##uname(struct page *page)      \
	{                                                                    \
		clear_bit(PG_##lname, &policy(page, 1)->flags);              \
	}

#define __SETPAGEFLAG(uname, lname, policy)                                  \
	static __always_inline void __folio_set_##lname(struct folio *folio) \
	{                                                                    \
		__set_bit(PG_##lname, folio_flags(folio, FOLIO_##policy));   \
	}                                                                    \
	static __always_inline void __SetPage##uname(struct page *page)      \
	{                                                                    \
		__set_bit(PG_##lname, &policy(page, 1)->flags);              \
	}

#define __CLEARPAGEFLAG(uname, lname, policy)                                  \
	static __always_inline void __folio_clear_##lname(struct folio *folio) \
	{                                                                      \
		__clear_bit(PG_##lname, folio_flags(folio, FOLIO_##policy));   \
	}                                                                      \
	static __always_inline void __ClearPage##uname(struct page *page)      \
	{                                                                      \
		__clear_bit(PG_##lname, &policy(page, 1)->flags);              \
	}

#define PAGEFLAG(uname, lname, policy)     \
	TESTPAGEFLAG(uname, lname, policy) \
	SETPAGEFLAG(uname, lname, policy) CLEARPAGEFLAG(uname, lname, policy)

#define __PAGEFLAG(uname, lname, policy)    \
	TESTPAGEFLAG(uname, lname, policy)  \
	__SETPAGEFLAG(uname, lname, policy) \
	__CLEARPAGEFLAG(uname, lname, policy)

/* clang-format off */
__PAGEFLAG(Locked, locked, PF_NO_TAIL)
__PAGEFLAG(Slab, slab, PF_NO_TAIL)
PAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
	__CLEARPAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
	__SETPAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
/* clang-format on */

#define __PG_HWPOISON 0

/*
 * On an anonymous page mapped into a user virtual memory area,
 * page->mapping points to its anon_vma, not to a struct address_space;
 * with the PAGE_MAPPING_ANON bit set to distinguish it.  See rmap.h.
 *
 * On an anonymous page in a VM_MERGEABLE area, if CONFIG_KSM is enabled,
 * the PAGE_MAPPING_MOVABLE bit may be set along with the PAGE_MAPPING_ANON
 * bit; and then page->mapping points, not to an anon_vma, but to a private
 * structure which KSM associates with that merged page.  See ksm.h.
 *
 * PAGE_MAPPING_KSM without PAGE_MAPPING_ANON is used for non-lru movable
 * page and then page->mapping points a struct address_space.
 *
 * Please note that, confusingly, "page_mapping" refers to the inode
 * address_space which maps the page from disk; whereas "page_mapped"
 * refers to user virtual address space into which the page is mapped.
 */
#define PAGE_MAPPING_ANON 0x1
#define PAGE_MAPPING_MOVABLE 0x2
#define PAGE_MAPPING_FLAGS (PAGE_MAPPING_ANON | PAGE_MAPPING_MOVABLE)

static __always_inline int PageMappingFlags(struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) != 0;
}

/**
 * folio_test_uptodate - Is this folio up to date?
 * @folio: The folio.
 *
 * The uptodate flag is set on a folio when every byte in the folio is
 * at least as new as the corresponding bytes on storage.  Anonymous
 * and CoW folios are always uptodate.  If the folio is not uptodate,
 * some of the bytes in it may be; see the is_partially_uptodate()
 * address_space operation.
 */
static inline bool folio_test_uptodate(struct folio *folio)
{
	bool ret = test_bit(PG_uptodate, folio_flags(folio, 0));

	/*
	 * Must ensure that the data we read out of the folio is loaded
	 * _after_ we've loaded folio->flags to check the uptodate bit.
	 * We can skip the barrier if the folio is not uptodate, because
	 * we wouldn't be reading anything from it.
	 *
	 * See folio_mark_uptodate() for the other side of the story.
	 */
	if (ret)
		smp_rmb();

	return ret;
}

static inline int PageUptodate(struct page *page)
{
	return folio_test_uptodate(page_folio(page));
}

static __always_inline void __folio_mark_uptodate(struct folio *folio)
{
	smp_wmb();
	__set_bit(PG_uptodate, folio_flags(folio, 0));
}

static __always_inline void folio_mark_uptodate(struct folio *folio)
{
	/*
	 * Memory barrier must be issued before setting the PG_uptodate bit,
	 * so that all previous stores issued in order to bring the folio
	 * uptodate are actually visible before folio_test_uptodate becomes true.
	 */
	smp_wmb();
	set_bit(PG_uptodate, folio_flags(folio, 0));
}

static __always_inline void __SetPageUptodate(struct page *page)
{
	__folio_mark_uptodate((struct folio *)page);
}

static __always_inline void SetPageUptodate(struct page *page)
{
	folio_mark_uptodate((struct folio *)page);
}

CLEARPAGEFLAG(Uptodate, uptodate, PF_NO_TAIL)

static __always_inline int PageHead(struct page *page)
{
	PF_POISONED_CHECK(page);
	return test_bit(PG_head, &page->flags) && !page_is_fake_head(page);
}

__SETPAGEFLAG(Head, head, PF_ANY)
__CLEARPAGEFLAG(Head, head, PF_ANY)
CLEARPAGEFLAG(Head, head, PF_ANY)

static __always_inline void set_compound_head(struct page *page,
					      struct page *head)
{
	WRITE_ONCE(page->compound_head, (unsigned long)head + 1);
}

static __always_inline void clear_compound_head(struct page *page)
{
	WRITE_ONCE(page->compound_head, 0);
}

/*
 * For pages that are never mapped to userspace (and aren't PageSlab),
 * page_type may be used.  Because it is initialised to -1, we invert the
 * sense of the bit, so __SetPageFoo *clears* the bit used for PageFoo, and
 * __ClearPageFoo *sets* the bit used for PageFoo.  We reserve a few high and
 * low bits so that an underflow or overflow of page_mapcount() won't be
 * mistaken for a page type value.
 */

#define PAGE_TYPE_BASE 0xf0000000
#define PG_buddy 0x00000080
#define PG_table 0x00000200

#define PageType(page, flag) \
	((page->page_type & (PAGE_TYPE_BASE | flag)) == PAGE_TYPE_BASE)

#define PAGE_TYPE_OPS(uname, lname)                                       \
	static __always_inline int Page##uname(struct page *page)         \
	{                                                                 \
		return PageType(page, PG_##lname);                        \
	}                                                                 \
	static __always_inline void __SetPage##uname(struct page *page)   \
	{                                                                 \
		page->page_type &= ~PG_##lname;                           \
	}                                                                 \
	static __always_inline void __ClearPage##uname(struct page *page) \
	{                                                                 \
		page->page_type |= PG_##lname;                            \
	}

/*
 * PageBuddy() indicates that the page is free and in the buddy system
 * (see mm/page_alloc.c).
 */
PAGE_TYPE_OPS(Buddy, buddy)

/*
 * Marks pages in use as page tables.
 */
PAGE_TYPE_OPS(Table, table)

#define PAGE_FLAGS_CHECK_AT_PREP (PAGEFLAGS_MASK & ~__PG_HWPOISON)

#undef PF_ANY
#undef PF_NO_TAIL
#undef PF_NO_COMPOUND
#endif /* !__GENERATING_BOUNDS_H */

#endif /* PAGE_FLAGS_H */
