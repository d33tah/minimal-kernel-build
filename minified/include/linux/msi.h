#ifndef LINUX_MSI_H
#define LINUX_MSI_H


#include <linux/cpumask.h>
#include <linux/xarray.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <asm/msi.h>

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

struct msi_desc {
	 
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

enum msi_desc_filter {
	 
	MSI_DESC_ALL,
	 
	MSI_DESC_NOTASSOCIATED,
	 
	MSI_DESC_ASSOCIATED,
};

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

static inline void msi_free_msi_descs(struct device *dev)
{
	msi_free_msi_descs_range(dev, MSI_DESC_ALL, 0, MSI_MAX_INDEX);
}

void __pci_read_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void __pci_write_msi_msg(struct msi_desc *entry, struct msi_msg *msg);

void pci_msi_mask_irq(struct irq_data *data);
void pci_msi_unmask_irq(struct irq_data *data);


bool arch_restore_msi_irqs(struct pci_dev *dev);


static inline struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev)
{
	return NULL;
}

#endif  
