 
#ifndef _ASM_X86_ACPI_H
#define _ASM_X86_ACPI_H

 


#include <asm/numa.h>
#include <asm/fixmap.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mpspec.h>
#include <asm/x86_init.h>



#define acpi_lapic 0
#define acpi_ioapic 0
#define acpi_disable_cmcff 0
/* acpi_noirq_set, acpi_disable_pci, disable_acpi removed - unused */

static inline void acpi_generic_reduced_hw_init(void) { }

static inline void x86_default_set_root_pointer(u64 addr) { }

static inline u64 x86_default_get_root_pointer(void)
{
	return 0;
}


#define ARCH_HAS_POWER_INIT	1

/* arch_apei_report_x86_error removed - unused */

#define ACPI_TABLE_UPGRADE_MAX_PHYS (max_low_pfn_mapped << PAGE_SHIFT)

#endif  
