#include <linux/init.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/stringify.h>
#include <asm/x86_init.h>

void __init pci_iommu_alloc(void)
{
}

static int __init pci_iommu_init(void)
{
	x86_init.iommu.iommu_init();

	return 0;
}
rootfs_initcall(pci_iommu_init);
