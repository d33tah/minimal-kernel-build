/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PCI_H
#define _ASM_X86_PCI_H

#include <linux/mm.h> /* for struct page */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/scatterlist.h>
#include <linux/numa.h>
#include <asm/io.h>
#include <asm/memtype.h>
#include <asm/x86_init.h>

struct pci_sysdata {
	int		domain;		/* PCI domain */
	int		node;		/* NUMA node */
#if IS_ENABLED(CONFIG_VMD)
	struct pci_dev	*vmd_dev;	/* VMD Device if in Intel VMD domain */
#endif
};

extern int pci_routeirq;
extern int noioapicquirk;
extern int noioapicreroute;

static inline struct pci_sysdata *to_pci_sysdata(const struct pci_bus *bus)
{
	return bus->sysdata;
}

static inline int pcibios_assign_all_busses(void) { return 0; }

extern unsigned long pci_mem_start;
#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		(pci_mem_start)

#define PCIBIOS_MIN_CARDBUS_IO	0x4000

extern int pcibios_enabled;
void pcibios_scan_root(int bus);

struct irq_routing_table *pcibios_get_irq_routing_table(void);
int pcibios_set_irq_routing(struct pci_dev *dev, int pin, int irq);


#define HAVE_PCI_MMAP
#define arch_can_pci_mmap_wc()	pat_enabled()
#define ARCH_GENERIC_PCI_MMAP_RESOURCE

static inline void early_quirks(void) { }

extern void pci_iommu_alloc(void);

/* generic pci stuff */
#include <asm-generic/pci.h>


struct pci_setup_rom {
	struct setup_data data;
	uint16_t vendor;
	uint16_t devid;
	uint64_t pcilen;
	unsigned long segment;
	unsigned long bus;
	unsigned long device;
	unsigned long function;
	uint8_t romdata[];
};

#endif /* _ASM_X86_PCI_H */
