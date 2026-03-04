
#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

#ifndef __GENERATING_BOUNDS_H
#include <linux/mm_types.h>
#include <generated/bounds.h>
#endif  

enum pageflags {
	PG_locked,
	PG_uptodate,
	PG_dirty,
	PG_active,
	PG_waiters,
	PG_error,
	PG_slab,
	PG_reserved,
	PG_head,
	PG_mappedtodisk,
	PG_reclaim,
	PG_swapbacked,
	__NR_PAGEFLAGS,
	PG_readahead = PG_reclaim,
	PG_anon_exclusive = PG_mappedtodisk,
};

#ifndef __GENERATING_BOUNDS_H

static inline unsigned long _compound_head(const struct page *page)
{
	unsigned long head = READ_ONCE(page->compound_head);

	if (unlikely(head & 1))
		return head - 1;
	return (unsigned long)page;
}

#define compound_head(page)	((typeof(page))_compound_head(page))

#define page_folio(p)		(_Generic((p),				\
	const struct page *:	(const struct folio *)_compound_head(p), \
	struct page *:		(struct folio *)_compound_head(p)))

#define folio_page(folio, n)	nth_page(&(folio)->page, n)

static unsigned long *folio_flags(struct folio *folio, unsigned n)
{
	struct page *page = &folio->page;

	return &page[n].flags;
}

#define PF_ANY(page, enforce)	(page)
#define PF_NO_COMPOUND(page, enforce)	(page)

/* Hand-written page flag functions - only the variants actually used */
static __always_inline bool folio_test_locked(struct folio *folio)
{ return test_bit(PG_locked, folio_flags(folio, 0)); }
static __always_inline void __folio_set_locked(struct folio *folio)
{ __set_bit(PG_locked, folio_flags(folio, 0)); }
static __always_inline void __folio_clear_locked(struct folio *folio)
{ __clear_bit(PG_locked, folio_flags(folio, 0)); }
static __always_inline void folio_clear_waiters(struct folio *folio)
{ clear_bit(PG_waiters, folio_flags(folio, 0)); }
static __always_inline void folio_clear_error(struct folio *folio)
{ clear_bit(PG_error, folio_flags(folio, 0)); }
static __always_inline bool folio_test_active(struct folio *folio)
{ return test_bit(PG_active, folio_flags(folio, 0)); }
static __always_inline void folio_set_active(struct folio *folio)
{ set_bit(PG_active, folio_flags(folio, 0)); }
static __always_inline void __folio_set_slab(struct folio *folio)
{ __set_bit(PG_slab, folio_flags(folio, 0)); }
static __always_inline void __ClearPageReserved(struct page *page)
{ __clear_bit(PG_reserved, &PF_NO_COMPOUND(page, 1)->flags); }
static __always_inline void __SetPageReserved(struct page *page)
{ __set_bit(PG_reserved, &PF_NO_COMPOUND(page, 1)->flags); }
static __always_inline bool folio_test_readahead(struct folio *folio)
{ return test_bit(PG_readahead, folio_flags(folio, 0)); }

#define PAGE_MAPPING_ANON	0x1
#define PAGE_MAPPING_FLAGS	0x3

static __always_inline int PageMappingFlags(struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) != 0;
}

#define PAGE_FLAGS_CHECK_AT_PREP	((1UL << __NR_PAGEFLAGS) - 1)

static inline bool folio_test_uptodate(struct folio *folio)
{
	bool ret = test_bit(PG_uptodate, folio_flags(folio, 0));
	 
	if (ret)
		smp_rmb();

	return ret;
}

static __always_inline void __folio_mark_uptodate(struct folio *folio)
{
	smp_wmb();
	__set_bit(PG_uptodate, folio_flags(folio, 0));
}

static __always_inline void folio_mark_uptodate(struct folio *folio)
{
	 
	smp_wmb();
	set_bit(PG_uptodate, folio_flags(folio, 0));
}

static __always_inline void __SetPageUptodate(struct page *page)
{
	__folio_mark_uptodate((struct folio *)page);
}

static __always_inline int PageHead(struct page *page)
{
	return test_bit(PG_head, &page->flags);
}

static __always_inline void __SetPageHead(struct page *page)
{ __set_bit(PG_head, &PF_ANY(page, 1)->flags); }

#define PAGE_TYPE_BASE	0xf0000000
#define PG_buddy	0x00000080
#define PG_table	0x00000200

#define PageType(page, flag)						\
	((page->page_type & (PAGE_TYPE_BASE | flag)) == PAGE_TYPE_BASE)

#define PAGE_TYPE_OPS(uname, lname)					\
static __always_inline int Page##uname(struct page *page)		\
{									\
	return PageType(page, PG_##lname);				\
}									\
static __always_inline void __SetPage##uname(struct page *page)		\
{									\
	VM_BUG_ON_PAGE(!PageType(page, 0), page);			\
	page->page_type &= ~PG_##lname;					\
}									\
static __always_inline void __ClearPage##uname(struct page *page)	\
{									\
	VM_BUG_ON_PAGE(!Page##uname(page), page);			\
	page->page_type |= PG_##lname;					\
}

PAGE_TYPE_OPS(Buddy, buddy)

PAGE_TYPE_OPS(Table, table)

#undef PF_ANY
#undef PF_NO_COMPOUND
#endif  

#endif	 
