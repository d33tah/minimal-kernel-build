 
#ifndef _ASM_X86_IOMMU_H
#define _ASM_X86_IOMMU_H

#include <linux/acpi.h>

#include <asm/e820/api.h>
#define x86_swiotlb_enable false

 
#define DMAR_OPERATION_TIMEOUT ((cycles_t) tsc_khz*10*1000)



#endif  
