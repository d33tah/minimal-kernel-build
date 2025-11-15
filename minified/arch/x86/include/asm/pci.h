/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PCI_H
#define _ASM_X86_PCI_H
/* Minimal stub version for x86 without CONFIG_PCI */

#include <linux/types.h>

struct pci_sysdata {
	int		domain;
	int		node;
};

struct pci_bus;
struct pci_dev;

/* Stubs */
extern int pci_routeirq;
extern int noioapicquirk;
extern int noioapicreroute;

static inline struct pci_sysdata *to_pci_sysdata(const struct pci_bus *bus) { return NULL; }
static inline int pcibios_assign_all_busses(void) { return 0; }

extern unsigned long pci_mem_start;
#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		(pci_mem_start)
#define PCIBIOS_MIN_CARDBUS_IO	0x4000

extern int pcibios_enabled;
static inline void pcibios_scan_root(int bus) { }

struct irq_routing_table;
static inline struct irq_routing_table *pcibios_get_irq_routing_table(void) { return NULL; }
static inline int pcibios_set_irq_routing(struct pci_dev *dev, int pin, int irq) { return -ENODEV; }

#define HAVE_PCI_MMAP
#define arch_can_pci_mmap_wc()	0
#define ARCH_GENERIC_PCI_MMAP_RESOURCE

static inline void early_quirks(void) { }
extern void pci_iommu_alloc(void);

struct pci_setup_rom {
	unsigned long data;
	uint16_t vendor;
	uint16_t devid;
};

#endif /* _ASM_X86_PCI_H */
