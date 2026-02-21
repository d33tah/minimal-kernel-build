
#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

#include <linux/mmdebug.h>
#ifndef __GENERATING_BOUNDS_H
#include <linux/mm_types.h>
#include <generated/bounds.h>
#endif  

enum pageflags {
	PG_locked,		 
	PG_referenced,
	PG_uptodate,
	PG_dirty,
	PG_lru,
	PG_active,
	PG_workingset,
	PG_waiters,		 
	PG_error,
	PG_slab,
	PG_owner_priv_1,	 
	PG_arch_1,
	PG_reserved,
	PG_private,		 
	PG_private_2,		 
	PG_writeback,		 
	PG_head,		 
	PG_mappedtodisk,	 
	PG_reclaim,		 
	PG_swapbacked,		 
	PG_unevictable,		 
	PG_mlocked,		 
	__NR_PAGEFLAGS,
	PG_readahead = PG_reclaim,
	PG_anon_exclusive = PG_mappedtodisk,
	PG_reported = PG_uptodate,
};

#define PAGEFLAGS_MASK		((1UL << NR_PAGEFLAGS) - 1)

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

static __always_inline int PageTail(struct page *page)
{
	return READ_ONCE(page->compound_head) & 1;
}

static __always_inline int PageCompound(struct page *page)
{
	return test_bit(PG_head, &page->flags) ||
	       READ_ONCE(page->compound_head) & 1;
}

#define	PAGE_POISON_PATTERN	-1l
static inline int PagePoisoned(const struct page *page)
{
	return READ_ONCE(page->flags) == PAGE_POISON_PATTERN;
}

static unsigned long *folio_flags(struct folio *folio, unsigned n)
{
	struct page *page = &folio->page;

	VM_BUG_ON_PGFLAGS(PageTail(page), page);
	VM_BUG_ON_PGFLAGS(n > 0 && !test_bit(PG_head, &page->flags), page);
	return &page[n].flags;
}

#define PF_POISONED_CHECK(page) ({					\
		VM_BUG_ON_PGFLAGS(PagePoisoned(page), page);		\
		page; })
#define PF_ANY(page, enforce)	PF_POISONED_CHECK(page)
#define PF_HEAD(page, enforce)	PF_POISONED_CHECK(compound_head(page))
#define PF_ONLY_HEAD(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(PageTail(page), page);		\
		PF_POISONED_CHECK(page); })
#define PF_NO_TAIL(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(enforce && PageTail(page), page);	\
		PF_POISONED_CHECK(compound_head(page)); })
#define PF_NO_COMPOUND(page, enforce) ({				\
		VM_BUG_ON_PGFLAGS(enforce && PageCompound(page), page);	\
		PF_POISONED_CHECK(page); })

#define FOLIO_PF_ANY		0
#define FOLIO_PF_HEAD		0
#define FOLIO_PF_ONLY_HEAD	0
#define FOLIO_PF_NO_TAIL	0
#define FOLIO_PF_NO_COMPOUND	0

#define TESTPAGEFLAG(uname, lname, policy)				\
static __always_inline bool folio_test_##lname(struct folio *folio)	\
{ return test_bit(PG_##lname, folio_flags(folio, FOLIO_##policy)); }	\
static __always_inline int Page##uname(struct page *page)		\
{ return test_bit(PG_##lname, &policy(page, 0)->flags); }

#define SETPAGEFLAG(uname, lname, policy)				\
static __always_inline							\
void folio_set_##lname(struct folio *folio)				\
{ set_bit(PG_##lname, folio_flags(folio, FOLIO_##policy)); }		\
static __always_inline void SetPage##uname(struct page *page)		\
{ set_bit(PG_##lname, &policy(page, 1)->flags); }

#define CLEARPAGEFLAG(uname, lname, policy)				\
static __always_inline							\
void folio_clear_##lname(struct folio *folio)				\
{ clear_bit(PG_##lname, folio_flags(folio, FOLIO_##policy)); }		\
static __always_inline void ClearPage##uname(struct page *page)		\
{ clear_bit(PG_##lname, &policy(page, 1)->flags); }

#define __SETPAGEFLAG(uname, lname, policy)				\
static __always_inline							\
void __folio_set_##lname(struct folio *folio)				\
{ __set_bit(PG_##lname, folio_flags(folio, FOLIO_##policy)); }		\
static __always_inline void __SetPage##uname(struct page *page)		\
{ __set_bit(PG_##lname, &policy(page, 1)->flags); }

#define __CLEARPAGEFLAG(uname, lname, policy)				\
static __always_inline							\
void __folio_clear_##lname(struct folio *folio)				\
{ __clear_bit(PG_##lname, folio_flags(folio, FOLIO_##policy)); }	\
static __always_inline void __ClearPage##uname(struct page *page)	\
{ __clear_bit(PG_##lname, &policy(page, 1)->flags); }

#define __PAGEFLAG(uname, lname, policy)				\
	TESTPAGEFLAG(uname, lname, policy)				\
	__SETPAGEFLAG(uname, lname, policy)				\
	__CLEARPAGEFLAG(uname, lname, policy)

__PAGEFLAG(Locked, locked, PF_NO_TAIL)
CLEARPAGEFLAG(Waiters, waiters, PF_ONLY_HEAD)
CLEARPAGEFLAG(Error, error, PF_NO_TAIL)
TESTPAGEFLAG(Dirty, dirty, PF_HEAD)
TESTPAGEFLAG(LRU, lru, PF_HEAD)
	SETPAGEFLAG(LRU, lru, PF_HEAD)
	__CLEARPAGEFLAG(LRU, lru, PF_HEAD)
TESTPAGEFLAG(Active, active, PF_HEAD)
	SETPAGEFLAG(Active, active, PF_HEAD)
	__CLEARPAGEFLAG(Active, active, PF_HEAD)
__SETPAGEFLAG(Slab, slab, PF_NO_TAIL)

__CLEARPAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
	__SETPAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
TESTPAGEFLAG(SwapBacked, swapbacked, PF_NO_TAIL)
	__SETPAGEFLAG(SwapBacked, swapbacked, PF_NO_TAIL)

TESTPAGEFLAG(Readahead, readahead, PF_NO_COMPOUND)

TESTPAGEFLAG(Unevictable, unevictable, PF_HEAD)
	__CLEARPAGEFLAG(Unevictable, unevictable, PF_HEAD)

TESTPAGEFLAG(Mlocked, mlocked, PF_NO_TAIL)
	__CLEARPAGEFLAG(Mlocked, mlocked, PF_NO_TAIL)

#define PAGE_MAPPING_ANON	0x1
#define PAGE_MAPPING_FLAGS	0x3

static __always_inline int PageMappingFlags(struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) != 0;
}

static __always_inline bool folio_test_anon(struct folio *folio)
{
	return ((unsigned long)folio->mapping & PAGE_MAPPING_ANON) != 0;
}

static __always_inline bool PageAnon(struct page *page)
{
	return folio_test_anon(page_folio(page));
}

static inline bool folio_test_uptodate(struct folio *folio)
{
	bool ret = test_bit(PG_uptodate, folio_flags(folio, 0));
	 
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

static __always_inline int PageHead(struct page *page)
{
	PF_POISONED_CHECK(page);
	return test_bit(PG_head, &page->flags);
}

__SETPAGEFLAG(Head, head, PF_ANY)

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

static __always_inline void SetPageAnonExclusive(struct page *page)
{
	/* PageKsm and PageHuge always return false */
	VM_BUG_ON_PGFLAGS(!PageAnon(page), page);
	set_bit(PG_anon_exclusive, &PF_ANY(page, 1)->flags);
}

#define PAGE_FLAGS_CHECK_AT_PREP	PAGEFLAGS_MASK

#undef PF_ANY
#undef PF_HEAD
#undef PF_ONLY_HEAD
#undef PF_NO_TAIL
#undef PF_NO_COMPOUND
#endif  

#endif	 
