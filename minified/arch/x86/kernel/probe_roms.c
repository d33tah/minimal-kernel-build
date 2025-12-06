/* Stub ROM probing */
#include <linux/pci.h>
#include <linux/init.h>

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
