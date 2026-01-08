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

/* __sg_flags, sg_assign_page, sg_set_page, sg_mark_end removed - unused */

#define for_each_sg(sglist, sg, nr, __i)	\
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

/* sg_init_marker removed - unused */

/* Basic functions - sg_next, sg_init_table, sg_init_one used */
/* sg_nents, sg_alloc_table, SG_MAX_SINGLE_ALLOC removed - never called */
struct scatterlist *sg_next(struct scatterlist *);
void sg_init_table(struct scatterlist *, unsigned int);
void sg_init_one(struct scatterlist *, const void *, unsigned int);

#endif  
