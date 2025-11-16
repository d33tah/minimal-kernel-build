 
 
#include <linux/init.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <asm/probe_roms.h>

void __iomem *pci_map_biosrom(struct pci_dev *pdev)
{
	return NULL;
}

void pci_unmap_biosrom(void __iomem *image)
{
}

void __init probe_roms(void)
{
}
