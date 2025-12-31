/* Minimal mpspec.h - SMP/APIC disabled */
#ifndef _ASM_X86_MPSPEC_H
#define _ASM_X86_MPSPEC_H

#include <asm/x86_init.h>
#include <asm/apicdef.h>

/* pic_mode, mp_bus_not_pci, phys_cpu_present_map,
   generic_processor_info removed - unused (SMP/APIC disabled) */

#define MAX_MP_BUSSES		32

extern unsigned int boot_cpu_physical_apicid;

#define smp_found_config 0
/* get_smp_config, find_smp_config, default_*_smp_config removed - never called */

/* physid_mask_t needed by struct apic */
#define PHYSID_ARRAY_SIZE	BITS_TO_LONGS(MAX_LOCAL_APIC)

struct physid_mask {
	unsigned long mask[PHYSID_ARRAY_SIZE];
};

typedef struct physid_mask physid_mask_t;

#define PHYSID_MASK_ALL		{ {[0 ... PHYSID_ARRAY_SIZE-1] = ~0UL} }
#define PHYSID_MASK_NONE	{ {[0 ... PHYSID_ARRAY_SIZE-1] = 0UL} }

#endif
