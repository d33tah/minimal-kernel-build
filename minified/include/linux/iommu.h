/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 */

#ifndef __LINUX_IOMMU_H
#define __LINUX_IOMMU_H

#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/of.h>



#define IOMMU_READ	(1 << 0)
#define IOMMU_WRITE	(1 << 1)
#define IOMMU_CACHE	(1 << 2) /* DMA cache coherency */
#define IOMMU_NOEXEC	(1 << 3)
#define IOMMU_MMIO	(1 << 4) /* e.g. things like MSI doorbells */
/*
 * Where the bus hardware includes a privilege level as part of its access type
 * markings, and certain devices are capable of issuing transactions marked as
 * either 'supervisor' or 'user', the IOMMU_PRIV flag requests that the other
 * given permission flags only apply to accesses at the higher privilege level,
 * and that unprivileged transactions should have as little access as possible.
 * This would usually imply the same permissions as kernel mappings on the CPU,
 * if the IOMMU page table format is equivalent.
 */
#define IOMMU_PRIV	(1 << 5)

struct iommu_ops;
struct iommu_group;
struct bus_type;
struct device;
struct iommu_domain;
struct iommu_domain_ops;
struct notifier_block;
struct iommu_sva;
struct iommu_fault_event;
struct iommu_dma_cookie;

/* iommu fault flags */
#define IOMMU_FAULT_READ	0x0
#define IOMMU_FAULT_WRITE	0x1

typedef int (*iommu_fault_handler_t)(struct iommu_domain *,
			struct device *, unsigned long, int, void *);
typedef int (*iommu_dev_fault_handler_t)(struct iommu_fault *, void *);

struct iommu_domain_geometry {
	dma_addr_t aperture_start; /* First address that can be mapped    */
	dma_addr_t aperture_end;   /* Last address that can be mapped     */
	bool force_aperture;       /* DMA only allowed in mappable range? */
};

/* Domain feature flags */
#define __IOMMU_DOMAIN_PAGING	(1U << 0)  /* Support for iommu_map/unmap */
#define __IOMMU_DOMAIN_DMA_API	(1U << 1)  /* Domain for use in DMA-API
					      implementation              */
#define __IOMMU_DOMAIN_PT	(1U << 2)  /* Domain is identity mapped   */
#define __IOMMU_DOMAIN_DMA_FQ	(1U << 3)  /* DMA-API uses flush queue    */

/*
 * This are the possible domain-types
 *
 *	IOMMU_DOMAIN_BLOCKED	- All DMA is blocked, can be used to isolate
 *				  devices
 *	IOMMU_DOMAIN_IDENTITY	- DMA addresses are system physical addresses
 *	IOMMU_DOMAIN_UNMANAGED	- DMA mappings managed by IOMMU-API user, used
 *				  for VMs
 *	IOMMU_DOMAIN_DMA	- Internally used for DMA-API implementations.
 *				  This flag allows IOMMU drivers to implement
 *				  certain optimizations for these domains
 *	IOMMU_DOMAIN_DMA_FQ	- As above, but definitely using batched TLB
 *				  invalidation.
 */
#define IOMMU_DOMAIN_BLOCKED	(0U)
#define IOMMU_DOMAIN_IDENTITY	(__IOMMU_DOMAIN_PT)
#define IOMMU_DOMAIN_UNMANAGED	(__IOMMU_DOMAIN_PAGING)
#define IOMMU_DOMAIN_DMA	(__IOMMU_DOMAIN_PAGING |	\
				 __IOMMU_DOMAIN_DMA_API)
#define IOMMU_DOMAIN_DMA_FQ	(__IOMMU_DOMAIN_PAGING |	\
				 __IOMMU_DOMAIN_DMA_API |	\
				 __IOMMU_DOMAIN_DMA_FQ)

struct iommu_domain {
	unsigned type;
	const struct iommu_domain_ops *ops;
	unsigned long pgsize_bitmap;	/* Bitmap of page sizes in use */
	iommu_fault_handler_t handler;
	void *handler_token;
	struct iommu_domain_geometry geometry;
	struct iommu_dma_cookie *iova_cookie;
};

static inline bool iommu_is_dma_domain(struct iommu_domain *domain)
{
	return domain->type & __IOMMU_DOMAIN_DMA_API;
}

enum iommu_cap {
	IOMMU_CAP_CACHE_COHERENCY,	/* IOMMU_CACHE is supported */
	IOMMU_CAP_INTR_REMAP,		/* IOMMU supports interrupt isolation */
	IOMMU_CAP_NOEXEC,		/* IOMMU_NOEXEC flag */
	IOMMU_CAP_PRE_BOOT_PROTECTION,	/* Firmware says it used the IOMMU for
					   DMA protection and we should too */
};

/* These are the possible reserved region types */
enum iommu_resv_type {
	/* Memory regions which must be mapped 1:1 at all times */
	IOMMU_RESV_DIRECT,
	/*
	 * Memory regions which are advertised to be 1:1 but are
	 * commonly considered relaxable in some conditions,
	 * for instance in device assignment use case (USB, Graphics)
	 */
	IOMMU_RESV_DIRECT_RELAXABLE,
	/* Arbitrary "never map this or give it to a device" address ranges */
	IOMMU_RESV_RESERVED,
	/* Hardware MSI region (untranslated) */
	IOMMU_RESV_MSI,
	/* Software-managed MSI translation window */
	IOMMU_RESV_SW_MSI,
};

/**
 * struct iommu_resv_region - descriptor for a reserved memory region
 * @list: Linked list pointers
 * @start: System physical start address of the region
 * @length: Length of the region in bytes
 * @prot: IOMMU Protection flags (READ/WRITE/...)
 * @type: Type of the reserved region
 */
struct iommu_resv_region {
	struct list_head	list;
	phys_addr_t		start;
	size_t			length;
	int			prot;
	enum iommu_resv_type	type;
};

/**
 * enum iommu_dev_features - Per device IOMMU features
 * @IOMMU_DEV_FEAT_SVA: Shared Virtual Addresses
 * @IOMMU_DEV_FEAT_IOPF: I/O Page Faults such as PRI or Stall. Generally
 *			 enabling %IOMMU_DEV_FEAT_SVA requires
 *			 %IOMMU_DEV_FEAT_IOPF, but some devices manage I/O Page
 *			 Faults themselves instead of relying on the IOMMU. When
 *			 supported, this feature must be enabled before and
 *			 disabled after %IOMMU_DEV_FEAT_SVA.
 *
 * Device drivers query whether a feature is supported using
 * iommu_dev_has_feature(), and enable it using iommu_dev_enable_feature().
 */
enum iommu_dev_features {
	IOMMU_DEV_FEAT_SVA,
	IOMMU_DEV_FEAT_IOPF,
};

#define IOMMU_PASID_INVALID	(-1U)


struct iommu_ops {};
struct iommu_group {};
struct iommu_fwspec {};
struct iommu_device {};
struct iommu_fault_param {};
struct iommu_iotlb_gather {};

static inline bool iommu_present(struct bus_type *bus)
{
	return false;
}

static inline bool device_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	return false;
}

static inline bool iommu_capable(struct bus_type *bus, enum iommu_cap cap)
{
	return false;
}

static inline struct iommu_domain *iommu_domain_alloc(struct bus_type *bus)
{
	return NULL;
}

static inline struct iommu_group *iommu_group_get_by_id(int id)
{
	return NULL;
}

static inline void iommu_domain_free(struct iommu_domain *domain)
{
}

static inline int iommu_attach_device(struct iommu_domain *domain,
				      struct device *dev)
{
	return -ENODEV;
}

static inline void iommu_detach_device(struct iommu_domain *domain,
				       struct device *dev)
{
}

static inline struct iommu_domain *iommu_get_domain_for_dev(struct device *dev)
{
	return NULL;
}

static inline int iommu_map(struct iommu_domain *domain, unsigned long iova,
			    phys_addr_t paddr, size_t size, int prot)
{
	return -ENODEV;
}

static inline int iommu_map_atomic(struct iommu_domain *domain,
				   unsigned long iova, phys_addr_t paddr,
				   size_t size, int prot)
{
	return -ENODEV;
}

static inline size_t iommu_unmap(struct iommu_domain *domain,
				 unsigned long iova, size_t size)
{
	return 0;
}

static inline size_t iommu_unmap_fast(struct iommu_domain *domain,
				      unsigned long iova, int gfp_order,
				      struct iommu_iotlb_gather *iotlb_gather)
{
	return 0;
}

static inline ssize_t iommu_map_sg(struct iommu_domain *domain,
				   unsigned long iova, struct scatterlist *sg,
				   unsigned int nents, int prot)
{
	return -ENODEV;
}

static inline ssize_t iommu_map_sg_atomic(struct iommu_domain *domain,
				  unsigned long iova, struct scatterlist *sg,
				  unsigned int nents, int prot)
{
	return -ENODEV;
}

static inline void iommu_flush_iotlb_all(struct iommu_domain *domain)
{
}

static inline void iommu_iotlb_sync(struct iommu_domain *domain,
				  struct iommu_iotlb_gather *iotlb_gather)
{
}

static inline phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	return 0;
}

static inline void iommu_set_fault_handler(struct iommu_domain *domain,
				iommu_fault_handler_t handler, void *token)
{
}

static inline void iommu_get_resv_regions(struct device *dev,
					struct list_head *list)
{
}

static inline void iommu_put_resv_regions(struct device *dev,
					struct list_head *list)
{
}

static inline int iommu_get_group_resv_regions(struct iommu_group *group,
					       struct list_head *head)
{
	return -ENODEV;
}

static inline void iommu_set_default_passthrough(bool cmd_line)
{
}

static inline void iommu_set_default_translated(bool cmd_line)
{
}

static inline bool iommu_default_passthrough(void)
{
	return true;
}

static inline int iommu_attach_group(struct iommu_domain *domain,
				     struct iommu_group *group)
{
	return -ENODEV;
}

static inline void iommu_detach_group(struct iommu_domain *domain,
				      struct iommu_group *group)
{
}

static inline struct iommu_group *iommu_group_alloc(void)
{
	return ERR_PTR(-ENODEV);
}

static inline void *iommu_group_get_iommudata(struct iommu_group *group)
{
	return NULL;
}

static inline void iommu_group_set_iommudata(struct iommu_group *group,
					     void *iommu_data,
					     void (*release)(void *iommu_data))
{
}

static inline int iommu_group_set_name(struct iommu_group *group,
				       const char *name)
{
	return -ENODEV;
}

static inline int iommu_group_add_device(struct iommu_group *group,
					 struct device *dev)
{
	return -ENODEV;
}

static inline void iommu_group_remove_device(struct device *dev)
{
}

static inline int iommu_group_for_each_dev(struct iommu_group *group,
					   void *data,
					   int (*fn)(struct device *, void *))
{
	return -ENODEV;
}

static inline struct iommu_group *iommu_group_get(struct device *dev)
{
	return NULL;
}

static inline void iommu_group_put(struct iommu_group *group)
{
}

static inline
int iommu_register_device_fault_handler(struct device *dev,
					iommu_dev_fault_handler_t handler,
					void *data)
{
	return -ENODEV;
}

static inline int iommu_unregister_device_fault_handler(struct device *dev)
{
	return 0;
}

static inline
int iommu_report_device_fault(struct device *dev, struct iommu_fault_event *evt)
{
	return -ENODEV;
}

static inline int iommu_page_response(struct device *dev,
				      struct iommu_page_response *msg)
{
	return -ENODEV;
}

static inline int iommu_group_id(struct iommu_group *group)
{
	return -ENODEV;
}

static inline int iommu_set_pgtable_quirks(struct iommu_domain *domain,
		unsigned long quirks)
{
	return 0;
}

static inline int iommu_device_register(struct iommu_device *iommu,
					const struct iommu_ops *ops,
					struct device *hwdev)
{
	return -ENODEV;
}

static inline struct iommu_device *dev_to_iommu_device(struct device *dev)
{
	return NULL;
}

static inline void iommu_iotlb_gather_init(struct iommu_iotlb_gather *gather)
{
}

static inline void iommu_iotlb_gather_add_page(struct iommu_domain *domain,
					       struct iommu_iotlb_gather *gather,
					       unsigned long iova, size_t size)
{
}

static inline bool iommu_iotlb_gather_queued(struct iommu_iotlb_gather *gather)
{
	return false;
}

static inline void iommu_device_unregister(struct iommu_device *iommu)
{
}

static inline int  iommu_device_sysfs_add(struct iommu_device *iommu,
					  struct device *parent,
					  const struct attribute_group **groups,
					  const char *fmt, ...)
{
	return -ENODEV;
}

static inline void iommu_device_sysfs_remove(struct iommu_device *iommu)
{
}

static inline int iommu_device_link(struct device *dev, struct device *link)
{
	return -EINVAL;
}

static inline void iommu_device_unlink(struct device *dev, struct device *link)
{
}

static inline int iommu_fwspec_init(struct device *dev,
				    struct fwnode_handle *iommu_fwnode,
				    const struct iommu_ops *ops)
{
	return -ENODEV;
}

static inline void iommu_fwspec_free(struct device *dev)
{
}

static inline int iommu_fwspec_add_ids(struct device *dev, u32 *ids,
				       int num_ids)
{
	return -ENODEV;
}

static inline
const struct iommu_ops *iommu_ops_from_fwnode(struct fwnode_handle *fwnode)
{
	return NULL;
}

static inline bool
iommu_dev_feature_enabled(struct device *dev, enum iommu_dev_features feat)
{
	return false;
}

static inline int
iommu_dev_enable_feature(struct device *dev, enum iommu_dev_features feat)
{
	return -ENODEV;
}

static inline int
iommu_dev_disable_feature(struct device *dev, enum iommu_dev_features feat)
{
	return -ENODEV;
}

static inline struct iommu_sva *
iommu_sva_bind_device(struct device *dev, struct mm_struct *mm, void *drvdata)
{
	return NULL;
}

static inline void iommu_sva_unbind_device(struct iommu_sva *handle)
{
}

static inline u32 iommu_sva_get_pasid(struct iommu_sva *handle)
{
	return IOMMU_PASID_INVALID;
}

static inline struct iommu_fwspec *dev_iommu_fwspec_get(struct device *dev)
{
	return NULL;
}

static inline int iommu_device_use_default_domain(struct device *dev)
{
	return 0;
}

static inline void iommu_device_unuse_default_domain(struct device *dev)
{
}

static inline int
iommu_group_claim_dma_owner(struct iommu_group *group, void *owner)
{
	return -ENODEV;
}

static inline void iommu_group_release_dma_owner(struct iommu_group *group)
{
}

static inline bool iommu_group_dma_owner_claimed(struct iommu_group *group)
{
	return false;
}

/**
 * iommu_map_sgtable - Map the given buffer to the IOMMU domain
 * @domain:	The IOMMU domain to perform the mapping
 * @iova:	The start address to map the buffer
 * @sgt:	The sg_table object describing the buffer
 * @prot:	IOMMU protection bits
 *
 * Creates a mapping at @iova for the buffer described by a scatterlist
 * stored in the given sg_table object in the provided IOMMU domain.
 */
static inline size_t iommu_map_sgtable(struct iommu_domain *domain,
			unsigned long iova, struct sg_table *sgt, int prot)
{
	return iommu_map_sg(domain, iova, sgt->sgl, sgt->orig_nents, prot);
}

static inline void iommu_debugfs_setup(void) {}

#endif /* __LINUX_IOMMU_H */
