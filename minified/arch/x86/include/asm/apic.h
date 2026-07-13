 
#ifndef _ASM_X86_APIC_H
#define _ASM_X86_APIC_H

#include <linux/cpumask.h>

#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <linux/atomic.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/msr.h>
#include <asm/hardirq.h>

/* Only keeping APIC functions that are actually called */
/* lapic_assign_legacy_vector removed - unused */
/* apic_needs_pit removed - unused (use_pit folded out, was const-true) */
/* Removed: lapic_shutdown, lapic_update_tsc_freq, check_x2apic */
/* x2apic_enabled removed - unused */


/*
 * struct apic body removed: never instantiated, no field is ever
 * accessed tree-wide, and the `apic` / `__apicdrivers` globals are
 * never referenced in this minified build. Kept as an opaque forward
 * declaration only.
 */
struct apic;

/* apic_read removed - unused */
/* apic_eoi() was an empty no-op stub; folded out of ack_APIC_irq */

static inline void ack_APIC_irq(void) {

}

#endif
