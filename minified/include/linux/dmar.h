/* Minimal dmar.h - IOMMU/DMAR stubs */
#ifndef __DMAR_H__
#define __DMAR_H__

#include <linux/types.h>

struct dmar_dev_scope;
struct intel_iommu;
struct irte;

static inline int dmar_device_add(void *handle) { return 0; }
static inline int dmar_device_remove(void *handle) { return 0; }
static inline bool dmar_platform_optin(void) { return false; }
static inline void detect_intel_iommu(void) { }

#endif
