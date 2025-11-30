/* Minimal dmar.h - IOMMU/DMAR stubs */
#ifndef __DMAR_H__
#define __DMAR_H__

#include <linux/types.h>

struct dmar_dev_scope;
struct intel_iommu;
struct irte;

/* dmar_device_add, dmar_device_remove, dmar_platform_optin removed - unused */
static inline void detect_intel_iommu(void) { }

#endif
