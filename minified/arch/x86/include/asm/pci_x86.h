/* Minimal pci_x86.h - PCI disabled */
#ifndef _ASM_X86_PCI_X86_H
#define _ASM_X86_PCI_X86_H

/* PCI init stubs - all NULL since PCI is disabled */
#define x86_default_pci_init		NULL
#define x86_default_pci_init_irq	NULL
#define x86_default_pci_fixup_irqs	NULL

#endif
