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

int sg_nents(struct scatterlist *sg)
{
	int nents;
	for (nents = 0; sg; sg = sg_next(sg))
		nents++;
	return nents;
}

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

/* Simplified allocation - only handles single entries (nents=1)
 * which is all that's used in kernel/dma/direct.c and mapping.c
 */
int sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp_mask)
{
	struct scatterlist *sg;

	memset(table, 0, sizeof(*table));

	if (nents == 0)
		return -EINVAL;

	/* Allocate the scatterlist array */
	sg = kmalloc_array(nents, sizeof(struct scatterlist), gfp_mask);
	if (unlikely(!sg))
		return -ENOMEM;

	sg_init_table(sg, nents);
	table->sgl = sg;
	table->nents = nents;
	table->orig_nents = nents;

	/* Mark the last entry */
	sg_mark_end(&sg[nents - 1]);

	return 0;
}
