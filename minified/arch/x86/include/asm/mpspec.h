/* Minimal mpspec.h - SMP/APIC disabled */
#ifndef _ASM_X86_MPSPEC_H
#define _ASM_X86_MPSPEC_H

#include <asm/x86_init.h>


/*
 * CONFIG_BASE_SMALL is fixed to 1 in this build (autoconf.h:262 / .config:439),
 * so `#if CONFIG_BASE_SMALL == 0` is always false and the 260 then-arm was
 * statically dead in every TU; only the #else value 32 was ever emitted.
 * Emit the live value unconditionally.
 */
# define MAX_MP_BUSSES		32

extern DECLARE_BITMAP(mp_bus_not_pci, MAX_MP_BUSSES);



static inline void get_smp_config(void)
{
	x86_init.mpparse.get_smp_config(0);
}

static inline void find_smp_config(void)
{
	x86_init.mpparse.find_smp_config();
}

#define default_find_smp_config x86_init_noop
#define default_get_smp_config x86_init_uint_noop

/* struct physid_mask / physid_mask_t / phys_cpu_present_map removed - 0-ref */

#endif
