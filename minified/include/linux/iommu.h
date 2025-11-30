/* Minimal IOMMU stub - CONFIG_IOMMU_API not enabled */

#ifndef __LINUX_IOMMU_H
#define __LINUX_IOMMU_H

#include <linux/types.h>
#include <linux/device.h>

/* Only functions actually used by the codebase */

static inline void iommu_set_default_passthrough(bool cmd_line)
{
}

static inline void iommu_set_default_translated(bool cmd_line)
{
}

static inline int iommu_device_use_default_domain(struct device *dev)
{
	return 0;
}

static inline void iommu_device_unuse_default_domain(struct device *dev)
{
}

#endif /* __LINUX_IOMMU_H */
