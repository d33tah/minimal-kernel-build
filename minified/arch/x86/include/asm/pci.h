 
#ifndef _ASM_X86_PCI_H
#define _ASM_X86_PCI_H
 

#include <linux/types.h>

struct pci_sysdata {
	int		domain;
	int		node;
};

struct pci_bus;
struct pci_dev;

extern unsigned long pci_mem_start;
#define PCIBIOS_MIN_MEM		(pci_mem_start)

static inline void early_quirks(void) { }
extern void pci_iommu_alloc(void);

#endif  
