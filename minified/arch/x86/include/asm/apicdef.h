/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal APIC definitions - only actually used symbols */
#ifndef _ASM_X86_APICDEF_H
#define _ASM_X86_APICDEF_H

/* Most APIC register definitions removed - not used in minimal kernel */

#define MAX_LOCAL_APIC 256
/* BAD_APICID removed - never used */

enum apic_delivery_modes {
	APIC_DELIVERY_MODE_FIXED	= 0,
};

#endif /* _ASM_X86_APICDEF_H */
