#ifndef _LINUX_SCATTERLIST_H
#define _LINUX_SCATTERLIST_H

#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <asm/io.h>

struct scatterlist {
	unsigned long	page_link;
	unsigned int	offset;
	unsigned int	length;
	dma_addr_t	dma_address;
	unsigned int	dma_length;
};

#define sg_dma_address(sg)	((sg)->dma_address)

#define sg_dma_len(sg)		((sg)->dma_length)

struct sg_table {
	struct scatterlist *sgl;
	unsigned int nents;
	unsigned int orig_nents;
};

#define SG_CHAIN	0x01UL
#define SG_END		0x02UL

#define SG_PAGE_LINK_MASK (SG_CHAIN | SG_END)

static inline unsigned int __sg_flags(struct scatterlist *sg)
{
	return sg->page_link & SG_PAGE_LINK_MASK;
}

static inline struct scatterlist *sg_chain_ptr(struct scatterlist *sg)
{
	return (struct scatterlist *)(sg->page_link & ~SG_PAGE_LINK_MASK);
}

static inline bool sg_is_chain(struct scatterlist *sg)
{
	return __sg_flags(sg) & SG_CHAIN;
}

static inline bool sg_is_last(struct scatterlist *sg)
{
	return __sg_flags(sg) & SG_END;
}

static inline void sg_assign_page(struct scatterlist *sg, struct page *page)
{
	unsigned long page_link = sg->page_link & (SG_CHAIN | SG_END);

	 
	BUG_ON((unsigned long)page & SG_PAGE_LINK_MASK);
	sg->page_link = page_link | (unsigned long) page;
}

static inline void sg_set_page(struct scatterlist *sg, struct page *page,
			       unsigned int len, unsigned int offset)
{
	sg_assign_page(sg, page);
	sg->offset = offset;
	sg->length = len;
}

static inline struct page *sg_page(struct scatterlist *sg)
{
	return (struct page *)((sg)->page_link & ~SG_PAGE_LINK_MASK);
}

static inline void sg_set_buf(struct scatterlist *sg, const void *buf,
			      unsigned int buflen)
{
	sg_set_page(sg, virt_to_page(buf), buflen, offset_in_page(buf));
}

#define for_each_sg(sglist, sg, nr, __i)	\
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

#define for_each_sgtable_sg(sgt, sg, i)		\
	for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)

#define for_each_sgtable_dma_sg(sgt, sg, i)	\
	for_each_sg((sgt)->sgl, sg, (sgt)->nents, i)


static inline void sg_mark_end(struct scatterlist *sg)
{
	 
	sg->page_link |= SG_END;
	sg->page_link &= ~SG_CHAIN;
}


static inline void sg_init_marker(struct scatterlist *sgl,
				  unsigned int nents)
{
	sg_mark_end(&sgl[nents - 1]);
}

/* Basic functions - only sg_nents, sg_next, sg_init_table, sg_init_one used */
int sg_nents(struct scatterlist *sg);
struct scatterlist *sg_next(struct scatterlist *);
void sg_init_table(struct scatterlist *, unsigned int);
void sg_init_one(struct scatterlist *, const void *, unsigned int);
int sg_alloc_table(struct sg_table *, unsigned int, gfp_t);

#define SG_MAX_SINGLE_ALLOC		(PAGE_SIZE / sizeof(struct scatterlist))

#endif  
