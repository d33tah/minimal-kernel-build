/* Minimal mpspec.h - SMP/APIC disabled */
#ifndef _ASM_X86_MPSPEC_H
#define _ASM_X86_MPSPEC_H

#include <asm/x86_init.h>
#include <asm/apicdef.h>

/* pic_mode, mp_bus_not_pci, phys_cpu_present_map,
   generic_processor_info, MAX_MP_BUSSES, smp_found_config removed - unused */

extern unsigned int boot_cpu_physical_apicid;

/* physid_mask_t needed by struct apic */
#define PHYSID_ARRAY_SIZE	BITS_TO_LONGS(MAX_LOCAL_APIC)

struct physid_mask {
	unsigned long mask[PHYSID_ARRAY_SIZE];
};

typedef struct physid_mask physid_mask_t;

/* PHYSID_MASK_ALL, PHYSID_MASK_NONE removed - unused */

#endif
