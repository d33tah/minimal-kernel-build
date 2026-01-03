
#include <linux/log2.h>

/* Removed: pcpu_post_unmap_tlb_flush, pcpu_depopulate_chunk
 * - Dead code since no chunk depopulation (~10 LOC) */
/* Removed: pcpu_populate_chunk - always returned 0, call sites simplified */

static struct pcpu_chunk *pcpu_create_chunk(gfp_t gfp)
{
	const int nr_pages = pcpu_group_sizes[0] >> PAGE_SHIFT;
	struct pcpu_chunk *chunk;
	struct page *pages;
	unsigned long flags;
	int i;

	chunk = pcpu_alloc_chunk(gfp);
	if (!chunk)
		return NULL;

	pages = alloc_pages(gfp, order_base_2(nr_pages));
	if (!pages) {
		pcpu_mem_free(chunk->md_blocks);
		pcpu_mem_free(chunk->bound_map);
		pcpu_mem_free(chunk->alloc_map);
		pcpu_mem_free(chunk);
		return NULL;
	}

	for (i = 0; i < nr_pages; i++)
		nth_page(pages, i)->index = (unsigned long)chunk;

	chunk->data = pages;
	chunk->base_addr = page_address(pages);

	spin_lock_irqsave(&pcpu_lock, flags);
	pcpu_chunk_populated(chunk, 0, nr_pages);
	spin_unlock_irqrestore(&pcpu_lock, flags);

	/* pcpu_stats_chunk_alloc removed - stats stub */
	return chunk;
}

/* Removed: pcpu_destroy_chunk, pcpu_addr_to_page - never called */

static int __init pcpu_verify_alloc_info(const struct pcpu_alloc_info *ai)
{
	size_t nr_pages, alloc_pages;

	if (ai->nr_groups != 1) {
		pr_crit("can't handle more than one group\n");
		return -EINVAL;
	}

	nr_pages = (ai->groups[0].nr_units * ai->unit_size) >> PAGE_SHIFT;
	alloc_pages = roundup_pow_of_two(nr_pages);

	if (alloc_pages > nr_pages)
		pr_warn("wasting %zu pages per chunk\n",
			alloc_pages - nr_pages);

	return 0;
}
