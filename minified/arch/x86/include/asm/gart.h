/* Minimal gart.h - only gart_iommu_hole_init is used (empty inline) */
#ifndef _ASM_X86_GART_H
#define _ASM_X86_GART_H

#include <asm/e820/api.h>

static inline void gart_iommu_hole_init(void) { }

#endif  
