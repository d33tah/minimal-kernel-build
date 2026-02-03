#ifndef LINUX_PCI_H
#define LINUX_PCI_H
/* mod_devicetable.h removed - was essentially empty */
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
/* Inlined from asm/pci.h - CONFIG_X86 always true */
/* struct pci_bus, pci_dev forward decls removed - never defined or used */
/* pci_iommu_alloc removed - empty stub, call site eliminated */
/* PCI macros, structs removed - unused */
/* pci_device_id, pci_printk/err/warn/etc removed - unused */
#include <linux/dma-mapping.h>
#endif
