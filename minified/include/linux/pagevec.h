
#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H

#define PAGEVEC_SIZE 15

struct page;
struct folio;

struct folio_batch {
	unsigned char nr;
	struct folio *folios[PAGEVEC_SIZE];
};

static inline void folio_batch_init(struct folio_batch *fbatch)
{
	fbatch->nr = 0;
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
