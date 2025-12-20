#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
#include <linux/iommu.h>
static inline void detect_intel_iommu(void) { }
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/pci.h>
static inline int amd_iommu_detect(void) { return -ENODEV; }

#include <asm/proto.h>
/* dma.h inlined into memblock.h */
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/x86_init.h>

const struct dma_map_ops *dma_ops;

int panic_on_overflow __read_mostly = 0;
int force_iommu __read_mostly = 0;

int iommu_merge __read_mostly = 0;

int no_iommu __read_mostly;
int iommu_detected __read_mostly = 0;

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
	swiotlb_init(x86_swiotlb_enable, x86_swiotlb_flags);
}


static int __init pci_iommu_init(void)
{
	x86_init.iommu.iommu_init();


	return 0;
}
rootfs_initcall(pci_iommu_init);

