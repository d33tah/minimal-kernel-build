/* Minimal mpspec.h - SMP/APIC disabled */
#ifndef _ASM_X86_MPSPEC_H
#define _ASM_X86_MPSPEC_H

#include <asm/x86_init.h>
#include <asm/apicdef.h>

/* pic_mode, mp_bus_not_pci, phys_cpu_present_map,
   generic_processor_info removed - unused (SMP/APIC disabled) */

#define MAX_MP_BUSSES		32

extern unsigned int boot_cpu_physical_apicid;
extern u8 boot_cpu_apic_version;
extern unsigned long mp_lapic_addr;

#define smp_found_config 0

static inline void get_smp_config(void)
{
	x86_init.mpparse.get_smp_config(0);
}

static inline void find_smp_config(void)
{
	x86_init.mpparse.find_smp_config();
}

static inline void e820__memblock_alloc_reserved_mpc_new(void) { }

#define default_find_smp_config x86_init_noop
#define default_get_smp_config x86_init_uint_noop

/* physid_mask_t needed by struct apic */
#define PHYSID_ARRAY_SIZE	BITS_TO_LONGS(MAX_LOCAL_APIC)

struct physid_mask {
	unsigned long mask[PHYSID_ARRAY_SIZE];
};

typedef struct physid_mask physid_mask_t;

#define PHYSID_MASK_ALL		{ {[0 ... PHYSID_ARRAY_SIZE-1] = ~0UL} }
#define PHYSID_MASK_NONE	{ {[0 ... PHYSID_ARRAY_SIZE-1] = 0UL} }

#endif
