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
#include <linux/pci_regs.h>

/* From uapi/linux/pci.h - inlined */
#define PCI_DEVFN(slot, func)	((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)		(((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)		((devfn) & 0x07)
#define PCIIOC_BASE		('P' << 24 | 'C' << 16 | 'I' << 8)
#define PCIIOC_CONTROLLER	(PCIIOC_BASE | 0x00)
#define PCIIOC_MMAP_IS_IO	(PCIIOC_BASE | 0x01)
#define PCIIOC_MMAP_IS_MEM	(PCIIOC_BASE | 0x02)
#define PCIIOC_WRITE_COMBINE	(PCIIOC_BASE | 0x03)

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

/* All PCI stub functions removed - CONFIG_PCI not set */

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
