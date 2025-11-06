// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub - ROM probing not needed
 */
#include <linux/init.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <asm/probe_roms.h>

void __iomem *pci_map_biosrom(struct pci_dev *pdev)
{
	return NULL;
}
EXPORT_SYMBOL(pci_map_biosrom);

void pci_unmap_biosrom(void __iomem *image)
{
}
EXPORT_SYMBOL(pci_unmap_biosrom);

void __init probe_roms(void)
{
}
