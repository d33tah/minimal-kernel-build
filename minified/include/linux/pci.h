 
#ifndef LINUX_PCI_H
#define LINUX_PCI_H
 

#include <linux/mod_devicetable.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/resource_ext.h>
#include <uapi/linux/pci.h>

 
#ifdef CONFIG_X86
#include <asm/pci.h>
#endif

 
#define PCI_DEVID(bus, devfn)	((((u16)(bus)) << 8) | (devfn))
#define PCI_BUS_NUM(x) (((x) >> 8) & 0xff)

 
struct pci_slot;
struct pci_bus;
struct pci_dev;
struct hotplug_slot;
struct pci_driver;
struct pci_ops;
struct pci_host_bridge;
struct pci_device_id;

enum pci_mmap_state {
	pci_mmap_io,
	pci_mmap_mem
};

enum pci_bus_speed {
	PCI_SPEED_UNKNOWN = 0,
};

enum pcie_link_width {
	PCIE_LNK_WIDTH_UNKNOWN = 0,
};

typedef unsigned int pci_power_t;

 
static inline void *pci_get_drvdata(struct pci_dev *pdev) { return NULL; }
static inline void pci_set_drvdata(struct pci_dev *pdev, void *data) { }
static inline const char *pci_name(const struct pci_dev *pdev) { return ""; }
static inline struct pci_dev *pci_dev_get(struct pci_dev *dev) { return NULL; }
static inline void pci_dev_put(struct pci_dev *dev) { }
static inline int pci_enable_device(struct pci_dev *dev) { return -ENODEV; }
static inline void pci_disable_device(struct pci_dev *dev) { }
static inline int pci_request_regions(struct pci_dev *pdev, const char *res_name) { return -ENODEV; }
static inline void pci_release_regions(struct pci_dev *pdev) { }
static inline void pci_set_master(struct pci_dev *dev) { }
static inline int pci_set_dma_mask(struct pci_dev *dev, u64 mask) { return -ENODEV; }
static inline int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask) { return -ENODEV; }
static inline void __iomem *pci_ioremap_bar(struct pci_dev *pdev, int bar) { return NULL; }
static inline void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen) { return NULL; }
static inline void pci_iounmap(struct pci_dev *dev, void __iomem *addr) { }

 
static inline struct irq_domain *pci_host_bridge_of_msi_domain(struct pci_bus *bus) { return NULL; }
static inline bool pci_host_of_has_msi_map(struct device *dev) { return false; }
static inline struct device_node *pci_device_to_OF_node(const struct pci_dev *pdev) { return NULL; }
static inline struct device_node *pci_bus_to_OF_node(struct pci_bus *bus) { return NULL; }
static inline struct irq_domain *pci_host_bridge_acpi_msi_domain(struct pci_bus *bus) { return NULL; }
static inline bool pci_pr3_present(struct pci_dev *pdev) { return false; }

 
static inline enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *dev) { return PCI_SPEED_UNKNOWN; }
static inline enum pcie_link_width pcie_get_width_cap(struct pci_dev *dev) { return PCIE_LNK_WIDTH_UNKNOWN; }

 
static inline int pci_enable_msi(struct pci_dev *dev) { return -ENOSYS; }
static inline void pci_disable_msi(struct pci_dev *dev) { }
static inline int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
		unsigned int max_vecs, unsigned int flags) { return -ENOSYS; }
static inline void pci_free_irq_vectors(struct pci_dev *dev) { }

 
static inline void pci_add_dma_alias(struct pci_dev *dev, u8 devfn_from, unsigned nr_devfns) { }
static inline bool pci_devs_are_dma_aliases(struct pci_dev *dev1, struct pci_dev *dev2) { return false; }
static inline int pci_for_each_dma_alias(struct pci_dev *pdev,
			   int (*fn)(struct pci_dev *pdev, u16 alias, void *data), void *data) { return 0; }

 
static inline int pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val) { *val = 0; return -ENODEV; }
static inline int pci_read_config_word(const struct pci_dev *dev, int where, u16 *val) { *val = 0; return -ENODEV; }
static inline int pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val) { *val = 0; return -ENODEV; }
static inline int pci_write_config_byte(const struct pci_dev *dev, int where, u8 val) { return -ENODEV; }
static inline int pci_write_config_word(const struct pci_dev *dev, int where, u16 val) { return -ENODEV; }
static inline int pci_write_config_dword(const struct pci_dev *dev, int where, u32 val) { return -ENODEV; }

#include <linux/dma-mapping.h>

#define pci_printk(level, pdev, fmt, arg...) \
	dev_printk(level, &(pdev)->dev, fmt, ##arg)

#define pci_emerg(pdev, fmt, arg...)	dev_emerg(&(pdev)->dev, fmt, ##arg)
#define pci_alert(pdev, fmt, arg...)	dev_alert(&(pdev)->dev, fmt, ##arg)
#define pci_crit(pdev, fmt, arg...)	dev_crit(&(pdev)->dev, fmt, ##arg)
#define pci_err(pdev, fmt, arg...)	dev_err(&(pdev)->dev, fmt, ##arg)
#define pci_warn(pdev, fmt, arg...)	dev_warn(&(pdev)->dev, fmt, ##arg)
#define pci_notice(pdev, fmt, arg...)	dev_notice(&(pdev)->dev, fmt, ##arg)
#define pci_info(pdev, fmt, arg...)	dev_info(&(pdev)->dev, fmt, ##arg)
#define pci_dbg(pdev, fmt, arg...)	dev_dbg(&(pdev)->dev, fmt, ##arg)

#endif  
