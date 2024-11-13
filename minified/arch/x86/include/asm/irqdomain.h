/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IRQDOMAIN_H
#define _ASM_IRQDOMAIN_H

#include <linux/irqdomain.h>
#include <asm/hw_irq.h>



static inline void x86_create_pci_msi_domain(void) { }
#define native_create_pci_msi_domain	NULL
#define x86_pci_msi_default_domain	NULL

#endif
