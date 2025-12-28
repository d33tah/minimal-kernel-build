#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
static inline void detect_intel_iommu(void)
{
}
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/pci.h>
static inline int amd_iommu_detect(void)
{
	return -ENODEV;
}

#include <asm/proto.h>
#define x86_swiotlb_enable false
/* Inlined from asm/gart.h */
static inline void gart_iommu_hole_init(void)
{
}
#include <asm/x86_init.h>

const struct dma_map_ops *dma_ops;

static inline void __init pci_swiotlb_detect(void)
{
}
#define x86_swiotlb_flags 0

void __init pci_iommu_alloc(void)
{
	pci_swiotlb_detect();
	gart_iommu_hole_init();
	amd_iommu_detect();
	detect_intel_iommu();
}

static int __init pci_iommu_init(void)
{
	x86_init.iommu.iommu_init();

	return 0;
}
rootfs_initcall(pci_iommu_init);
