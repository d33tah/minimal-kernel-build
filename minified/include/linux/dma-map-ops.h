 
 
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
	struct sg_table *(*alloc_noncontiguous)(struct device *dev, size_t size,
			enum dma_data_direction dir, gfp_t gfp,
			unsigned long attrs);
	void (*free_noncontiguous)(struct device *dev, size_t size,
			struct sg_table *sgt, enum dma_data_direction dir);
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
	dma_addr_t (*map_resource)(struct device *dev, phys_addr_t phys_addr,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
	void (*unmap_resource)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
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
static inline void set_dma_ops(struct device *dev,
			       const struct dma_map_ops *dma_ops)
{
}

static inline struct cma *dev_get_cma_area(struct device *dev)
{
	return NULL;
}
static inline void dma_contiguous_reserve(phys_addr_t limit)
{
}
static inline int dma_contiguous_reserve_area(phys_addr_t size,
		phys_addr_t base, phys_addr_t limit, struct cma **res_cma,
		bool fixed)
{
	return -ENOSYS;
}
static inline struct page *dma_alloc_from_contiguous(struct device *dev,
		size_t count, unsigned int order, bool no_warn)
{
	return NULL;
}
static inline bool dma_release_from_contiguous(struct device *dev,
		struct page *pages, int count)
{
	return false;
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

static inline void dma_pernuma_cma_reserve(void) { }

static inline int dma_declare_coherent_memory(struct device *dev,
		phys_addr_t phys_addr, dma_addr_t device_addr, size_t size)
{
	return -ENOSYS;
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
static inline int dma_mmap_from_global_coherent(struct vm_area_struct *vma,
		void *cpu_addr, size_t size, int *ret)
{
	return 0;
}

 
struct dma_sgt_handle {
	struct sg_table sgt;
	struct page **pages;
};
#define sgt_handle(sgt) \
	container_of((sgt), struct dma_sgt_handle, sgt)

int dma_common_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int dma_common_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
struct page *dma_common_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp);
void dma_common_free_pages(struct device *dev, size_t size, struct page *vaddr,
		dma_addr_t dma_handle, enum dma_data_direction dir);

struct page **dma_common_find_pages(void *cpu_addr);
void *dma_common_contiguous_remap(struct page *page, size_t size, pgprot_t prot,
		const void *caller);
void *dma_common_pages_remap(struct page **pages, size_t size, pgprot_t prot,
		const void *caller);
void dma_common_free_remap(void *cpu_addr, size_t size);

struct page *dma_alloc_from_pool(struct device *dev, size_t size,
		void **cpu_addr, gfp_t flags,
		bool (*phys_addr_ok)(struct device *, phys_addr_t, size_t));
bool dma_free_from_pool(struct device *dev, void *start, size_t size);

int dma_direct_set_offset(struct device *dev, phys_addr_t cpu_start,
		dma_addr_t dma_start, u64 size);

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
extern bool dma_default_coherent;
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return dev->dma_coherent;
}
#else
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return true;
}
#endif  

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

static inline void arch_setup_dma_ops(struct device *dev, u64 dma_base,
		u64 size, const struct iommu_ops *iommu, bool coherent)
{
}

static inline void arch_teardown_dma_ops(struct device *dev)
{
}

static inline void dma_debug_add_bus(struct bus_type *bus)
{
}
static inline void debug_dma_dump_mappings(struct device *dev)
{
}

extern const struct dma_map_ops dma_dummy_ops;

#endif  
