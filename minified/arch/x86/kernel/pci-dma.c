#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <asm/proto.h>
#include <asm/x86_init.h>

/* detect_intel_iommu, amd_iommu_detect, gart_iommu_hole_init,
   pci_swiotlb_detect, pci_iommu_init removed - all empty stubs */

void __init pci_iommu_alloc(void)
{
	/* No IOMMU detection needed for minimal kernel */
}
