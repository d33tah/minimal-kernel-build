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
#include <linux/acpi.h>
/* Inlined from asm/pci.h */
#ifdef CONFIG_X86
struct pci_bus;
struct pci_dev;
extern void pci_iommu_alloc(void);
#endif
/* PCI macros, structs removed - unused */
struct pci_bus;
struct pci_dev;
/* pci_device_id, pci_printk/err/warn/etc removed - unused */
#include <linux/dma-mapping.h>
#endif
