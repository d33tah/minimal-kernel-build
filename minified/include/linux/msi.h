/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MSI_H
#define LINUX_MSI_H

/*
 * This header file contains MSI data structures and functions which are
 * only relevant for:
 *	- Interrupt core code
 *	- PCI/MSI core code
 *	- MSI interrupt domain implementations
 *	- IOMMU, low level VFIO, NTB and other justified exceptions
 *	  dealing with low level MSI details.
 *
 * Regular device drivers have no business with any of these functions and
 * especially storing MSI descriptor pointers in random code is considered
 * abuse. The only function which is relevant for drivers is msi_get_virq().
 */

#include <linux/cpumask.h>
#include <linux/xarray.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <asm/msi.h>

/* Dummy shadow structures if an architecture does not define them */
#ifndef arch_msi_msg_addr_lo
typedef struct arch_msi_msg_addr_lo {
	u32	address_lo;
} __attribute__ ((packed)) arch_msi_msg_addr_lo_t;
#endif

#ifndef arch_msi_msg_addr_hi
typedef struct arch_msi_msg_addr_hi {
	u32	address_hi;
} __attribute__ ((packed)) arch_msi_msg_addr_hi_t;
#endif

#ifndef arch_msi_msg_data
typedef struct arch_msi_msg_data {
	u32	data;
} __attribute__ ((packed)) arch_msi_msg_data_t;
#endif

/**
 * msi_msg - Representation of a MSI message
 * @address_lo:		Low 32 bits of msi message address
 * @arch_addrlo:	Architecture specific shadow of @address_lo
 * @address_hi:		High 32 bits of msi message address
 *			(only used when device supports it)
 * @arch_addrhi:	Architecture specific shadow of @address_hi
 * @data:		MSI message data (usually 16 bits)
 * @arch_data:		Architecture specific shadow of @data
 */
struct msi_msg {
	union {
		u32			address_lo;
		arch_msi_msg_addr_lo_t	arch_addr_lo;
	};
	union {
		u32			address_hi;
		arch_msi_msg_addr_hi_t	arch_addr_hi;
	};
	union {
		u32			data;
		arch_msi_msg_data_t	arch_data;
	};
};

extern int pci_msi_ignore_mask;
/* Helper functions */
struct irq_data;
struct msi_desc;
struct pci_dev;
struct platform_msi_priv_data;
struct device_attribute;

void __get_cached_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
static inline void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg)
{
}

typedef void (*irq_write_msi_msg_t)(struct msi_desc *desc,
				    struct msi_msg *msg);

/**
 * pci_msi_desc - PCI/MSI specific MSI descriptor data
 *
 * @msi_mask:	[PCI MSI]   MSI cached mask bits
 * @msix_ctrl:	[PCI MSI-X] MSI-X cached per vector control bits
 * @is_msix:	[PCI MSI/X] True if MSI-X
 * @multiple:	[PCI MSI/X] log2 num of messages allocated
 * @multi_cap:	[PCI MSI/X] log2 num of messages supported
 * @can_mask:	[PCI MSI/X] Masking supported?
 * @is_64:	[PCI MSI/X] Address size: 0=32bit 1=64bit
 * @default_irq:[PCI MSI/X] The default pre-assigned non-MSI irq
 * @mask_pos:	[PCI MSI]   Mask register position
 * @mask_base:	[PCI MSI-X] Mask register base address
 */
struct pci_msi_desc {
	union {
		u32 msi_mask;
		u32 msix_ctrl;
	};
	struct {
		u8	is_msix		: 1;
		u8	multiple	: 3;
		u8	multi_cap	: 3;
		u8	can_mask	: 1;
		u8	is_64		: 1;
		u8	is_virtual	: 1;
		unsigned default_irq;
	} msi_attrib;
	union {
		u8	mask_pos;
		void __iomem *mask_base;
	};
};

#define MSI_MAX_INDEX		((unsigned int)USHRT_MAX)

/**
 * struct msi_desc - Descriptor structure for MSI based interrupts
 * @irq:	The base interrupt number
 * @nvec_used:	The number of vectors used
 * @dev:	Pointer to the device which uses this descriptor
 * @msg:	The last set MSI message cached for reuse
 * @affinity:	Optional pointer to a cpu affinity mask for this descriptor
 * @sysfs_attr:	Pointer to sysfs device attribute
 *
 * @write_msi_msg:	Callback that may be called when the MSI message
 *			address or data changes
 * @write_msi_msg_data:	Data parameter for the callback.
 *
 * @msi_index:	Index of the msi descriptor
 * @pci:	PCI specific msi descriptor data
 */
struct msi_desc {
	/* Shared device/bus type independent data */
	unsigned int			irq;
	unsigned int			nvec_used;
	struct device			*dev;
	struct msi_msg			msg;
	struct irq_affinity_desc	*affinity;

	void (*write_msi_msg)(struct msi_desc *entry, void *data);
	void *write_msi_msg_data;

	u16				msi_index;
	struct pci_msi_desc		pci;
};

/*
 * Filter values for the MSI descriptor iterators and accessor functions.
 */
enum msi_desc_filter {
	/* All descriptors */
	MSI_DESC_ALL,
	/* Descriptors which have no interrupt associated */
	MSI_DESC_NOTASSOCIATED,
	/* Descriptors which have an interrupt associated */
	MSI_DESC_ASSOCIATED,
};

/**
 * msi_device_data - MSI per device data
 * @properties:		MSI properties which are interesting to drivers
 * @platform_data:	Platform-MSI specific data
 * @mutex:		Mutex protecting the MSI descriptor store
 * @__store:		Xarray for storing MSI descriptor pointers
 * @__iter_idx:		Index to search the next entry for iterators
 */
struct msi_device_data {
	unsigned long			properties;
	struct platform_msi_priv_data	*platform_data;
	struct mutex			mutex;
	struct xarray			__store;
	unsigned long			__iter_idx;
};

int msi_setup_device_data(struct device *dev);

unsigned int msi_get_virq(struct device *dev, unsigned int index);
void msi_lock_descs(struct device *dev);
void msi_unlock_descs(struct device *dev);

struct msi_desc *msi_first_desc(struct device *dev, enum msi_desc_filter filter);
struct msi_desc *msi_next_desc(struct device *dev, enum msi_desc_filter filter);

/**
 * msi_for_each_desc - Iterate the MSI descriptors
 *
 * @desc:	struct msi_desc pointer used as iterator
 * @dev:	struct device pointer - device to iterate
 * @filter:	Filter for descriptor selection
 *
 * Notes:
 *  - The loop must be protected with a msi_lock_descs()/msi_unlock_descs()
 *    pair.
 *  - It is safe to remove a retrieved MSI descriptor in the loop.
 */
#define msi_for_each_desc(desc, dev, filter)			\
	for ((desc) = msi_first_desc((dev), (filter)); (desc);	\
	     (desc) = msi_next_desc((dev), (filter)))

#define msi_desc_to_dev(desc)		((desc)->dev)

static inline const void *msi_desc_get_iommu_cookie(struct msi_desc *desc)
{
	return NULL;
}

static inline void msi_desc_set_iommu_cookie(struct msi_desc *desc,
					     const void *iommu_cookie)
{
}

static inline void pci_write_msi_msg(unsigned int irq, struct msi_msg *msg)
{
}

int msi_add_msi_desc(struct device *dev, struct msi_desc *init_desc);
void msi_free_msi_descs_range(struct device *dev, enum msi_desc_filter filter,
			      unsigned int first_index, unsigned int last_index);

/**
 * msi_free_msi_descs - Free MSI descriptors of a device
 * @dev:	Device to free the descriptors
 */
static inline void msi_free_msi_descs(struct device *dev)
{
	msi_free_msi_descs_range(dev, MSI_DESC_ALL, 0, MSI_MAX_INDEX);
}

void __pci_read_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void __pci_write_msi_msg(struct msi_desc *entry, struct msi_msg *msg);

void pci_msi_mask_irq(struct irq_data *data);
void pci_msi_unmask_irq(struct irq_data *data);

/*
 * The arch hooks to setup up msi irqs. Default functions are implemented
 * as weak symbols so that they /can/ be overriden by architecture specific
 * code if needed. These hooks can only be enabled by the architecture.
 *
 * If CONFIG_PCI_MSI_ARCH_FALLBACKS is not selected they are replaced by
 * stubs with warnings.
 */

/*
 * The restore hook is still available even for fully irq domain based
 * setups. Courtesy to XEN/X86.
 */
bool arch_restore_msi_irqs(struct pci_dev *dev);


static inline struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev)
{
	return NULL;
}

#endif /* LINUX_MSI_H */
