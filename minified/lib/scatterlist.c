/* Minimal stub for scatterlist - scatter-gather list management
 * Used only by kernel/dma for single-entry allocations
 * Original: 620 LOC, Stubbed to minimal single-entry support
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>

/* Basic scatterlist navigation */
struct scatterlist *sg_next(struct scatterlist *sg)
{
	if (sg_is_last(sg))
		return NULL;
	sg++;
	if (unlikely(sg_is_chain(sg)))
		sg = sg_chain_ptr(sg);
	return sg;
}

/* sg_nents removed - never called */

/* Initialize scatterlist table */
void sg_init_table(struct scatterlist *sgl, unsigned int nents)
{
	memset(sgl, 0, sizeof(*sgl) * nents);
	sg_init_marker(sgl, nents);
}

void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int buflen)
{
	sg_init_table(sg, 1);
	sg_set_buf(sg, buf, buflen);
}

/* sg_alloc_table removed - never called */
