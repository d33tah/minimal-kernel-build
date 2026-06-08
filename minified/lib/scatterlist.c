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

