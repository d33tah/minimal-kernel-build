#ifndef _LINUX_DMA_MAP_OPS_H
#define _LINUX_DMA_MAP_OPS_H

#include <linux/dma-mapping.h>
#include <linux/pgtable.h>

struct cma;

struct dma_map_ops {
	void *(*alloc)(struct device *dev, size_t size,
			dma_addr_t *dma_handle, gfp_t gfp,
			unsigned long attrs);
	void (*free)(struct device *dev, size_t size, void *vaddr,
			dma_addr_t dma_handle, unsigned long attrs);
	struct page *(*alloc_pages)(struct device *dev, size_t size,
			dma_addr_t *dma_handle, enum dma_data_direction dir,
			gfp_t gfp);
	void (*free_pages)(struct device *dev, size_t size, struct page *vaddr,
			dma_addr_t dma_handle, enum dma_data_direction dir);
	/* alloc_noncontiguous, free_noncontiguous removed - unused */
	int (*mmap)(struct device *, struct vm_area_struct *,
			void *, dma_addr_t, size_t, unsigned long attrs);

	int (*get_sgtable)(struct device *dev, struct sg_table *sgt,
			void *cpu_addr, dma_addr_t dma_addr, size_t size,
			unsigned long attrs);

	dma_addr_t (*map_page)(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction dir, unsigned long attrs);
	void (*unmap_page)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
	 
	int (*map_sg)(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir, unsigned long attrs);
	void (*unmap_sg)(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir, unsigned long attrs);
	/* map_resource, unmap_resource removed - unused */
	void (*sync_single_for_cpu)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir);
	void (*sync_single_for_device)(struct device *dev,
			dma_addr_t dma_handle, size_t size,
			enum dma_data_direction dir);
	void (*sync_sg_for_cpu)(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir);
	void (*sync_sg_for_device)(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir);
	void (*cache_sync)(struct device *dev, void *vaddr, size_t size,
			enum dma_data_direction direction);
	int (*dma_supported)(struct device *dev, u64 mask);
	u64 (*get_required_mask)(struct device *dev);
	size_t (*max_mapping_size)(struct device *dev);
	unsigned long (*get_merge_boundary)(struct device *dev);
};

static inline const struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return NULL;
}

static inline struct cma *dev_get_cma_area(struct device *dev)
{
	return NULL;
}
static inline void dma_contiguous_reserve(phys_addr_t limit)
{
}
static inline struct page *dma_alloc_contiguous(struct device *dev, size_t size,
		gfp_t gfp)
{
	return NULL;
}
static inline void dma_free_contiguous(struct device *dev, struct page *page,
		size_t size)
{
	__free_pages(page, get_order(size));
}

#define dma_alloc_from_dev_coherent(dev, size, handle, ret) (0)
#define dma_release_from_dev_coherent(dev, order, vaddr) (0)
#define dma_mmap_from_dev_coherent(dev, vma, vaddr, order, ret) (0)

static inline void *dma_alloc_from_global_coherent(struct device *dev,
		ssize_t size, dma_addr_t *dma_handle)
{
	return NULL;
}
static inline int dma_release_from_global_coherent(int order, void *vaddr)
{
	return 0;
}

/* Only keeping functions actually used in kernel/dma/ */
void *dma_common_contiguous_remap(struct page *page, size_t size, pgprot_t prot,
		const void *caller);
void *dma_common_pages_remap(struct page **pages, size_t size, pgprot_t prot,
		const void *caller);
void dma_common_free_remap(void *cpu_addr, size_t size);
struct page **dma_common_find_pages(void *cpu_addr);

struct page *dma_alloc_from_pool(struct device *dev, size_t size,
		void **cpu_addr, gfp_t flags,
		bool (*phys_addr_ok)(struct device *, phys_addr_t, size_t));
bool dma_free_from_pool(struct device *dev, void *start, size_t size);

/* SYNC_DMA_FOR_* not defined */
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return true;
}

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void arch_dma_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs);

#ifndef pgprot_dmacoherent
#define pgprot_dmacoherent(prot)	pgprot_noncached(prot)
#endif

pgprot_t dma_pgprot(struct device *dev, pgprot_t prot, unsigned long attrs);

static inline void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
}

static inline void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
}

static inline void arch_sync_dma_for_cpu_all(void)
{
}

static inline void arch_dma_prep_coherent(struct page *page, size_t size)
{
}

static inline void arch_dma_mark_clean(phys_addr_t paddr, size_t size)
{
}

void *arch_dma_set_uncached(void *addr, size_t size);
void arch_dma_clear_uncached(void *addr, size_t size);

#define arch_dma_map_page_direct(d, a)		(false)
#define arch_dma_unmap_page_direct(d, a)	(false)
#define arch_dma_map_sg_direct(d, s, n)		(false)
#define arch_dma_unmap_sg_direct(d, s, n)	(false)

static inline void arch_teardown_dma_ops(struct device *dev)
{
}

extern const struct dma_map_ops dma_dummy_ops;

#endif  
