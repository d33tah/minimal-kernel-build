 
#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>
#include <linux/bug.h>
#include <linux/mem_encrypt.h>

 

 
#define DMA_ATTR_WEAK_ORDERING		(1UL << 1)
 
#define DMA_ATTR_WRITE_COMBINE		(1UL << 2)
 
#define DMA_ATTR_NO_KERNEL_MAPPING	(1UL << 4)
 
#define DMA_ATTR_SKIP_CPU_SYNC		(1UL << 5)
 
#define DMA_ATTR_FORCE_CONTIGUOUS	(1UL << 6)
 
#define DMA_ATTR_ALLOC_SINGLE_PAGES	(1UL << 7)
 
#define DMA_ATTR_NO_WARN	(1UL << 8)

 
#define DMA_ATTR_PRIVILEGED		(1UL << 9)

 
#define DMA_MAPPING_ERROR		(~(dma_addr_t)0)

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))

static inline void debug_dma_mapping_error(struct device *dev,
		dma_addr_t dma_addr)
{
}
static inline void debug_dma_map_single(struct device *dev, const void *addr,
		unsigned long len)
{
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	debug_dma_mapping_error(dev, dma_addr);

	if (unlikely(dma_addr == DMA_MAPPING_ERROR))
		return -ENOMEM;
	return 0;
}

dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page,
		size_t offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs);
void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
unsigned int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
				      int nents, enum dma_data_direction dir,
				      unsigned long attrs);
int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs);
dma_addr_t dma_map_resource(struct device *dev, phys_addr_t phys_addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_resource(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir);
void dma_sync_single_for_device(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir);
void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
		    int nelems, enum dma_data_direction dir);
void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
		       int nelems, enum dma_data_direction dir);
void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t flag, unsigned long attrs);
void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_handle, unsigned long attrs);
void *dmam_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void dmam_free_coherent(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle);
int dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
bool dma_can_mmap(struct device *dev);
int dma_supported(struct device *dev, u64 mask);
int dma_set_mask(struct device *dev, u64 mask);
int dma_set_coherent_mask(struct device *dev, u64 mask);
u64 dma_get_required_mask(struct device *dev);
size_t dma_max_mapping_size(struct device *dev);
bool dma_need_sync(struct device *dev, dma_addr_t dma_addr);
unsigned long dma_get_merge_boundary(struct device *dev);
struct sg_table *dma_alloc_noncontiguous(struct device *dev, size_t size,
		enum dma_data_direction dir, gfp_t gfp, unsigned long attrs);
void dma_free_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir);
void *dma_vmap_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt);
void dma_vunmap_noncontiguous(struct device *dev, void *vaddr);
int dma_mmap_noncontiguous(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct sg_table *sgt);

struct page *dma_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp);
void dma_free_pages(struct device *dev, size_t size, struct page *page,
		dma_addr_t dma_handle, enum dma_data_direction dir);
int dma_mmap_pages(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct page *page);


static inline dma_addr_t dma_map_single_attrs(struct device *dev, void *ptr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	 
	if (dev_WARN_ONCE(dev, is_vmalloc_addr(ptr),
			  "rejecting DMA map of vmalloc memory\n"))
		return DMA_MAPPING_ERROR;
	debug_dma_map_single(dev, ptr, size);
	return dma_map_page_attrs(dev, virt_to_page(ptr), offset_in_page(ptr),
			size, dir, attrs);
}



#define dma_map_single(d, a, s, r) dma_map_single_attrs(d, a, s, r, 0)
#define dma_unmap_single(d, a, s, r) dma_unmap_single_attrs(d, a, s, r, 0)
#define dma_map_sg(d, s, n, r) dma_map_sg_attrs(d, s, n, r, 0)
#define dma_unmap_sg(d, s, n, r) dma_unmap_sg_attrs(d, s, n, r, 0)
#define dma_map_page(d, p, o, s, r) dma_map_page_attrs(d, p, o, s, r, 0)
#define dma_unmap_page(d, a, s, r) dma_unmap_page_attrs(d, a, s, r, 0)
#define dma_get_sgtable(d, t, v, h, s) dma_get_sgtable_attrs(d, t, v, h, s, 0)
#define dma_mmap_coherent(d, v, c, h, s) dma_mmap_attrs(d, v, c, h, s, 0)

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp)
{
	return dma_alloc_attrs(dev, size, dma_handle, gfp,
			(gfp & __GFP_NOWARN) ? DMA_ATTR_NO_WARN : 0);
}

static inline void dma_free_coherent(struct device *dev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle)
{
	return dma_free_attrs(dev, size, cpu_addr, dma_handle, 0);
}


static inline u64 dma_get_mask(struct device *dev)
{
	if (dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	return DMA_BIT_MASK(32);
}


 
static inline bool dma_addressing_limited(struct device *dev)
{
	return min_not_zero(dma_get_mask(dev), dev->bus_dma_limit) <
			    dma_get_required_mask(dev);
}


static inline int dma_get_cache_alignment(void)
{
#ifdef ARCH_DMA_MINALIGN
	return ARCH_DMA_MINALIGN;
#endif
	return 1;
}


#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)
#define dma_unmap_addr(PTR, ADDR_NAME)           (0)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  do { } while (0)
#define dma_unmap_len(PTR, LEN_NAME)             (0)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    do { } while (0)

#endif  
