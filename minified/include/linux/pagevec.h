
#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H

#include <linux/xarray.h>

#define PAGEVEC_SIZE	15

struct page;
struct folio;

struct pagevec {
	unsigned char nr;
	bool percpu_pvec_drained;
	struct page *pages[PAGEVEC_SIZE];
};

void __pagevec_lru_add(struct pagevec *pvec);

/* pagevec_reinit removed: 0-caller static-inline orphan */

static inline unsigned pagevec_count(struct pagevec *pvec)
{
	return pvec->nr;
}

static inline unsigned pagevec_add(struct pagevec *pvec, struct page *page)
{
	pvec->pages[pvec->nr++] = page;
	return PAGEVEC_SIZE - pvec->nr;
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

#endif  
