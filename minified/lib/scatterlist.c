 
 
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/kmemleak.h>

 
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

 
int sg_nents_for_len(struct scatterlist *sg, u64 len)
{
	int nents;
	u64 total;

	if (!len)
		return 0;

	for (nents = 0, total = 0; sg; sg = sg_next(sg)) {
		nents++;
		total += sg->length;
		if (total >= len)
			return nents;
	}

	return -EINVAL;
}

 
struct scatterlist *sg_last(struct scatterlist *sgl, unsigned int nents)
{
	struct scatterlist *sg, *ret = NULL;
	unsigned int i;

	for_each_sg(sgl, sg, nents, i)
		ret = sg;

	BUG_ON(!sg_is_last(ret));
	return ret;
}

 
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

 
static struct scatterlist *sg_kmalloc(unsigned int nents, gfp_t gfp_mask)
{
	if (nents == SG_MAX_SINGLE_ALLOC) {
		 
		void *ptr = (void *) __get_free_page(gfp_mask);
		kmemleak_alloc(ptr, PAGE_SIZE, 1, gfp_mask);
		return ptr;
	} else
		return kmalloc_array(nents, sizeof(struct scatterlist),
				     gfp_mask);
}

static void sg_kfree(struct scatterlist *sg, unsigned int nents)
{
	if (nents == SG_MAX_SINGLE_ALLOC) {
		kmemleak_free(sg);
		free_page((unsigned long) sg);
	} else
		kfree(sg);
}

 
void __sg_free_table(struct sg_table *table, unsigned int max_ents,
		     unsigned int nents_first_chunk, sg_free_fn *free_fn,
		     unsigned int num_ents)
{
	struct scatterlist *sgl, *next;
	unsigned curr_max_ents = nents_first_chunk ?: max_ents;

	if (unlikely(!table->sgl))
		return;

	sgl = table->sgl;
	while (num_ents) {
		unsigned int alloc_size = num_ents;
		unsigned int sg_size;

		 
		if (alloc_size > curr_max_ents) {
			next = sg_chain_ptr(&sgl[curr_max_ents - 1]);
			alloc_size = curr_max_ents;
			sg_size = alloc_size - 1;
		} else {
			sg_size = alloc_size;
			next = NULL;
		}

		num_ents -= sg_size;
		if (nents_first_chunk)
			nents_first_chunk = 0;
		else
			free_fn(sgl, alloc_size);
		sgl = next;
		curr_max_ents = max_ents;
	}

	table->sgl = NULL;
}

 
void sg_free_append_table(struct sg_append_table *table)
{
	__sg_free_table(&table->sgt, SG_MAX_SINGLE_ALLOC, false, sg_kfree,
			table->total_nents);
}


 
void sg_free_table(struct sg_table *table)
{
	__sg_free_table(table, SG_MAX_SINGLE_ALLOC, false, sg_kfree,
			table->orig_nents);
}

 
int __sg_alloc_table(struct sg_table *table, unsigned int nents,
		     unsigned int max_ents, struct scatterlist *first_chunk,
		     unsigned int nents_first_chunk, gfp_t gfp_mask,
		     sg_alloc_fn *alloc_fn)
{
	struct scatterlist *sg, *prv;
	unsigned int left;
	unsigned curr_max_ents = nents_first_chunk ?: max_ents;
	unsigned prv_max_ents;

	memset(table, 0, sizeof(*table));

	if (nents == 0)
		return -EINVAL;

	left = nents;
	prv = NULL;
	do {
		unsigned int sg_size, alloc_size = left;

		if (alloc_size > curr_max_ents) {
			alloc_size = curr_max_ents;
			sg_size = alloc_size - 1;
		} else
			sg_size = alloc_size;

		left -= sg_size;

		if (first_chunk) {
			sg = first_chunk;
			first_chunk = NULL;
		} else {
			sg = alloc_fn(alloc_size, gfp_mask);
		}
		if (unlikely(!sg)) {
			 
			if (prv)
				table->nents = ++table->orig_nents;

			return -ENOMEM;
		}

		sg_init_table(sg, alloc_size);
		table->nents = table->orig_nents += sg_size;

		 
		if (prv)
			sg_chain(prv, prv_max_ents, sg);
		else
			table->sgl = sg;

		 
		if (!left)
			sg_mark_end(&sg[sg_size - 1]);

		prv = sg;
		prv_max_ents = curr_max_ents;
		curr_max_ents = max_ents;
	} while (left);

	return 0;
}

 
int sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp_mask)
{
	int ret;

	ret = __sg_alloc_table(table, nents, SG_MAX_SINGLE_ALLOC,
			       NULL, 0, gfp_mask, sg_kmalloc);
	if (unlikely(ret))
		sg_free_table(table);
	return ret;
}

static struct scatterlist *get_next_sg(struct sg_append_table *table,
				       struct scatterlist *cur,
				       unsigned long needed_sges,
				       gfp_t gfp_mask)
{
	struct scatterlist *new_sg, *next_sg;
	unsigned int alloc_size;

	if (cur) {
		next_sg = sg_next(cur);
		 
		if (!sg_is_last(next_sg) || needed_sges == 1)
			return next_sg;
	}

	alloc_size = min_t(unsigned long, needed_sges, SG_MAX_SINGLE_ALLOC);
	new_sg = sg_kmalloc(alloc_size, gfp_mask);
	if (!new_sg)
		return ERR_PTR(-ENOMEM);
	sg_init_table(new_sg, alloc_size);
	if (cur) {
		table->total_nents += alloc_size - 1;
		__sg_chain(next_sg, new_sg);
	} else {
		table->sgt.sgl = new_sg;
		table->total_nents = alloc_size;
	}
	return new_sg;
}

 
int sg_alloc_append_table_from_pages(struct sg_append_table *sgt_append,
		struct page **pages, unsigned int n_pages, unsigned int offset,
		unsigned long size, unsigned int max_segment,
		unsigned int left_pages, gfp_t gfp_mask)
{
	unsigned int chunks, cur_page, seg_len, i, prv_len = 0;
	unsigned int added_nents = 0;
	struct scatterlist *s = sgt_append->prv;

	 
	max_segment = ALIGN_DOWN(max_segment, PAGE_SIZE);
	if (WARN_ON(max_segment < PAGE_SIZE))
		return -EINVAL;

	if (IS_ENABLED(CONFIG_ARCH_NO_SG_CHAIN) && sgt_append->prv)
		return -EOPNOTSUPP;

	if (sgt_append->prv) {
		unsigned long paddr =
			(page_to_pfn(sg_page(sgt_append->prv)) * PAGE_SIZE +
			 sgt_append->prv->offset + sgt_append->prv->length) /
			PAGE_SIZE;

		if (WARN_ON(offset))
			return -EINVAL;

		 
		prv_len = sgt_append->prv->length;
		while (n_pages && page_to_pfn(pages[0]) == paddr) {
			if (sgt_append->prv->length + PAGE_SIZE > max_segment)
				break;
			sgt_append->prv->length += PAGE_SIZE;
			paddr++;
			pages++;
			n_pages--;
		}
		if (!n_pages)
			goto out;
	}

	 
	chunks = 1;
	seg_len = 0;
	for (i = 1; i < n_pages; i++) {
		seg_len += PAGE_SIZE;
		if (seg_len >= max_segment ||
		    page_to_pfn(pages[i]) != page_to_pfn(pages[i - 1]) + 1) {
			chunks++;
			seg_len = 0;
		}
	}

	 
	cur_page = 0;
	for (i = 0; i < chunks; i++) {
		unsigned int j, chunk_size;

		 
		seg_len = 0;
		for (j = cur_page + 1; j < n_pages; j++) {
			seg_len += PAGE_SIZE;
			if (seg_len >= max_segment ||
			    page_to_pfn(pages[j]) !=
			    page_to_pfn(pages[j - 1]) + 1)
				break;
		}

		 
		s = get_next_sg(sgt_append, s, chunks - i + left_pages,
				gfp_mask);
		if (IS_ERR(s)) {
			 
			if (sgt_append->prv)
				sgt_append->prv->length = prv_len;
			return PTR_ERR(s);
		}
		chunk_size = ((j - cur_page) << PAGE_SHIFT) - offset;
		sg_set_page(s, pages[cur_page],
			    min_t(unsigned long, size, chunk_size), offset);
		added_nents++;
		size -= chunk_size;
		offset = 0;
		cur_page = j;
	}
	sgt_append->sgt.nents += added_nents;
	sgt_append->sgt.orig_nents = sgt_append->sgt.nents;
	sgt_append->prv = s;
out:
	if (!left_pages)
		sg_mark_end(s);
	return 0;
}

 
int sg_alloc_table_from_pages_segment(struct sg_table *sgt, struct page **pages,
				unsigned int n_pages, unsigned int offset,
				unsigned long size, unsigned int max_segment,
				gfp_t gfp_mask)
{
	struct sg_append_table append = {};
	int err;

	err = sg_alloc_append_table_from_pages(&append, pages, n_pages, offset,
					       size, max_segment, 0, gfp_mask);
	if (err) {
		sg_free_append_table(&append);
		return err;
	}
	memcpy(sgt, &append.sgt, sizeof(*sgt));
	WARN_ON(append.total_nents != sgt->orig_nents);
	return 0;
}


void __sg_page_iter_start(struct sg_page_iter *piter,
			  struct scatterlist *sglist, unsigned int nents,
			  unsigned long pgoffset)
{
	piter->__pg_advance = 0;
	piter->__nents = nents;

	piter->sg = sglist;
	piter->sg_pgoffset = pgoffset;
}

static int sg_page_count(struct scatterlist *sg)
{
	return PAGE_ALIGN(sg->offset + sg->length) >> PAGE_SHIFT;
}

bool __sg_page_iter_next(struct sg_page_iter *piter)
{
	if (!piter->__nents || !piter->sg)
		return false;

	piter->sg_pgoffset += piter->__pg_advance;
	piter->__pg_advance = 1;

	while (piter->sg_pgoffset >= sg_page_count(piter->sg)) {
		piter->sg_pgoffset -= sg_page_count(piter->sg);
		piter->sg = sg_next(piter->sg);
		if (!--piter->__nents || !piter->sg)
			return false;
	}

	return true;
}

static int sg_dma_page_count(struct scatterlist *sg)
{
	return PAGE_ALIGN(sg->offset + sg_dma_len(sg)) >> PAGE_SHIFT;
}

bool __sg_page_iter_dma_next(struct sg_dma_page_iter *dma_iter)
{
	struct sg_page_iter *piter = &dma_iter->base;

	if (!piter->__nents || !piter->sg)
		return false;

	piter->sg_pgoffset += piter->__pg_advance;
	piter->__pg_advance = 1;

	while (piter->sg_pgoffset >= sg_dma_page_count(piter->sg)) {
		piter->sg_pgoffset -= sg_dma_page_count(piter->sg);
		piter->sg = sg_next(piter->sg);
		if (!--piter->__nents || !piter->sg)
			return false;
	}

	return true;
}

 
void sg_miter_start(struct sg_mapping_iter *miter, struct scatterlist *sgl,
		    unsigned int nents, unsigned int flags)
{
	memset(miter, 0, sizeof(struct sg_mapping_iter));

	__sg_page_iter_start(&miter->piter, sgl, nents, 0);
	WARN_ON(!(flags & (SG_MITER_TO_SG | SG_MITER_FROM_SG)));
	miter->__flags = flags;
}

static bool sg_miter_get_next_page(struct sg_mapping_iter *miter)
{
	if (!miter->__remaining) {
		struct scatterlist *sg;

		if (!__sg_page_iter_next(&miter->piter))
			return false;

		sg = miter->piter.sg;

		miter->__offset = miter->piter.sg_pgoffset ? 0 : sg->offset;
		miter->piter.sg_pgoffset += miter->__offset >> PAGE_SHIFT;
		miter->__offset &= PAGE_SIZE - 1;
		miter->__remaining = sg->offset + sg->length -
				     (miter->piter.sg_pgoffset << PAGE_SHIFT) -
				     miter->__offset;
		miter->__remaining = min_t(unsigned long, miter->__remaining,
					   PAGE_SIZE - miter->__offset);
	}

	return true;
}

 
bool sg_miter_skip(struct sg_mapping_iter *miter, off_t offset)
{
	sg_miter_stop(miter);

	while (offset) {
		off_t consumed;

		if (!sg_miter_get_next_page(miter))
			return false;

		consumed = min_t(off_t, offset, miter->__remaining);
		miter->__offset += consumed;
		miter->__remaining -= consumed;
		offset -= consumed;
	}

	return true;
}

 
bool sg_miter_next(struct sg_mapping_iter *miter)
{
	sg_miter_stop(miter);

	 
	if (!sg_miter_get_next_page(miter))
		return false;

	miter->page = sg_page_iter_page(&miter->piter);
	miter->consumed = miter->length = miter->__remaining;

	if (miter->__flags & SG_MITER_ATOMIC)
		miter->addr = kmap_atomic(miter->page) + miter->__offset;
	else
		miter->addr = kmap(miter->page) + miter->__offset;

	return true;
}

 
void sg_miter_stop(struct sg_mapping_iter *miter)
{
	WARN_ON(miter->consumed > miter->length);

	 
	if (miter->addr) {
		miter->__offset += miter->consumed;
		miter->__remaining -= miter->consumed;

		if (miter->__flags & SG_MITER_TO_SG)
			flush_dcache_page(miter->page);

		if (miter->__flags & SG_MITER_ATOMIC) {
			WARN_ON_ONCE(!pagefault_disabled());
			kunmap_atomic(miter->addr);
		} else
			kunmap(miter->page);

		miter->page = NULL;
		miter->addr = NULL;
		miter->length = 0;
		miter->consumed = 0;
	}
}

 
size_t sg_copy_buffer(struct scatterlist *sgl, unsigned int nents, void *buf,
		      size_t buflen, off_t skip, bool to_buffer)
{
	unsigned int offset = 0;
	struct sg_mapping_iter miter;
	unsigned int sg_flags = SG_MITER_ATOMIC;

	if (to_buffer)
		sg_flags |= SG_MITER_FROM_SG;
	else
		sg_flags |= SG_MITER_TO_SG;

	sg_miter_start(&miter, sgl, nents, sg_flags);

	if (!sg_miter_skip(&miter, skip))
		return 0;

	while ((offset < buflen) && sg_miter_next(&miter)) {
		unsigned int len;

		len = min(miter.length, buflen - offset);

		if (to_buffer)
			memcpy(buf + offset, miter.addr, len);
		else
			memcpy(miter.addr, buf + offset, len);

		offset += len;
	}

	sg_miter_stop(&miter);

	return offset;
}

 
size_t sg_copy_from_buffer(struct scatterlist *sgl, unsigned int nents,
			   const void *buf, size_t buflen)
{
	return sg_copy_buffer(sgl, nents, (void *)buf, buflen, 0, false);
}

 
size_t sg_copy_to_buffer(struct scatterlist *sgl, unsigned int nents,
			 void *buf, size_t buflen)
{
	return sg_copy_buffer(sgl, nents, buf, buflen, 0, true);
}

 
size_t sg_pcopy_from_buffer(struct scatterlist *sgl, unsigned int nents,
			    const void *buf, size_t buflen, off_t skip)
{
	return sg_copy_buffer(sgl, nents, (void *)buf, buflen, skip, false);
}

 
size_t sg_pcopy_to_buffer(struct scatterlist *sgl, unsigned int nents,
			  void *buf, size_t buflen, off_t skip)
{
	return sg_copy_buffer(sgl, nents, buf, buflen, skip, true);
}

 
size_t sg_zero_buffer(struct scatterlist *sgl, unsigned int nents,
		       size_t buflen, off_t skip)
{
	unsigned int offset = 0;
	struct sg_mapping_iter miter;
	unsigned int sg_flags = SG_MITER_ATOMIC | SG_MITER_TO_SG;

	sg_miter_start(&miter, sgl, nents, sg_flags);

	if (!sg_miter_skip(&miter, skip))
		return false;

	while (offset < buflen && sg_miter_next(&miter)) {
		unsigned int len;

		len = min(miter.length, buflen - offset);
		memset(miter.addr, 0, len);

		offset += len;
	}

	sg_miter_stop(&miter);
	return offset;
}
