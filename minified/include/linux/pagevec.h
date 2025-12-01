
#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H

#include <linux/xarray.h>

#define PAGEVEC_SIZE	15

struct page;
struct folio;
struct address_space;

struct pagevec {
	unsigned char nr;
	bool percpu_pvec_drained;
	struct page *pages[PAGEVEC_SIZE];
};

void __pagevec_release(struct pagevec *pvec);
void __pagevec_lru_add(struct pagevec *pvec);
unsigned pagevec_lookup_range(struct pagevec *pvec,
			      struct address_space *mapping,
			      pgoff_t *start, pgoff_t end);
static inline unsigned pagevec_lookup(struct pagevec *pvec,
				      struct address_space *mapping,
				      pgoff_t *start)
{
	return pagevec_lookup_range(pvec, mapping, start, (pgoff_t)-1);
}

unsigned pagevec_lookup_range_tag(struct pagevec *pvec,
		struct address_space *mapping, pgoff_t *index, pgoff_t end,
		xa_mark_t tag);
static inline unsigned pagevec_lookup_tag(struct pagevec *pvec,
		struct address_space *mapping, pgoff_t *index, xa_mark_t tag)
{
	return pagevec_lookup_range_tag(pvec, mapping, index, (pgoff_t)-1, tag);
}

static inline void pagevec_init(struct pagevec *pvec)
{
	pvec->nr = 0;
	pvec->percpu_pvec_drained = false;
}

static inline void pagevec_reinit(struct pagevec *pvec)
{
	pvec->nr = 0;
}

static inline unsigned pagevec_count(struct pagevec *pvec)
{
	return pvec->nr;
}

static inline unsigned pagevec_space(struct pagevec *pvec)
{
	return PAGEVEC_SIZE - pvec->nr;
}

static inline unsigned pagevec_add(struct pagevec *pvec, struct page *page)
{
	pvec->pages[pvec->nr++] = page;
	return pagevec_space(pvec);
}

static inline void pagevec_release(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_release(pvec);
}

struct folio_batch {
	unsigned char nr;
	bool percpu_pvec_drained;
	struct folio *folios[PAGEVEC_SIZE];
};

static_assert(sizeof(struct pagevec) == sizeof(struct folio_batch));
static_assert(offsetof(struct pagevec, pages) ==
		offsetof(struct folio_batch, folios));

static inline void folio_batch_init(struct folio_batch *fbatch)
{
	fbatch->nr = 0;
	fbatch->percpu_pvec_drained = false;
}

static inline unsigned int folio_batch_count(struct folio_batch *fbatch)
{
	return fbatch->nr;
}

static inline unsigned int fbatch_space(struct folio_batch *fbatch)
{
	return PAGEVEC_SIZE - fbatch->nr;
}

static inline unsigned folio_batch_add(struct folio_batch *fbatch,
		struct folio *folio)
{
	fbatch->folios[fbatch->nr++] = folio;
	return fbatch_space(fbatch);
}

static inline void folio_batch_release(struct folio_batch *fbatch)
{
	pagevec_release((struct pagevec *)fbatch);
}

void folio_batch_remove_exceptionals(struct folio_batch *fbatch);
#endif  
