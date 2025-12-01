#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
#include <linux/iommu.h>
#include <linux/dmar.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/amd-iommu.h>

#include <asm/proto.h>
#include <asm/dma.h>
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

static inline void __init pci_xen_swiotlb_init(void)
{
}

void __init pci_iommu_alloc(void)
{
	pci_swiotlb_detect();
	gart_iommu_hole_init();
	amd_iommu_detect();
	detect_intel_iommu();
	swiotlb_init(x86_swiotlb_enable, x86_swiotlb_flags);
}

/* Stub: iommu= cmdline option not needed for minimal kernel */
static __init int iommu_setup(char *p) { return 0; }
early_param("iommu", iommu_setup);

static int __init pci_iommu_init(void)
{
	x86_init.iommu.iommu_init();


	return 0;
}
rootfs_initcall(pci_iommu_init);

