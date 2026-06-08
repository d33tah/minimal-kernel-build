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

#define for_each_sg(sglist, sg, nr, __i)	\
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

/* Basic functions - only sg_next used (via for_each_sg) */
struct scatterlist *sg_next(struct scatterlist *);

#endif
