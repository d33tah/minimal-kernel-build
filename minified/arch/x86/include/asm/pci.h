 
#ifndef _ASM_X86_PCI_H
#define _ASM_X86_PCI_H
 

#include <linux/types.h>

struct pci_bus;

 
extern int pci_routeirq;
extern int noioapicquirk;
extern int noioapicreroute;

/* PCIBIOS_MIN_IO/MIN_CARDBUS_IO, HAVE_PCI_MMAP, arch_can_pci_mmap_wc,
 * ARCH_GENERIC_PCI_MMAP_RESOURCE removed - 0-ref (no PCI mmap consumer) */

extern int pcibios_enabled;

#endif
