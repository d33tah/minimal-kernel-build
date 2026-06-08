/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal APIC definitions - only actually used symbols */
#ifndef _ASM_X86_APICDEF_H
#define _ASM_X86_APICDEF_H

/* Only keeping definitions that are actually used */

#define MAX_IO_APICS 64
#define MAX_LOCAL_APIC 256

#define BAD_APICID 0xFFu

enum apic_delivery_modes {
	APIC_DELIVERY_MODE_FIXED	= 0,
	APIC_DELIVERY_MODE_LOWESTPRIO   = 1,
	APIC_DELIVERY_MODE_SMI		= 2,
	APIC_DELIVERY_MODE_NMI		= 4,
	APIC_DELIVERY_MODE_INIT		= 5,
	APIC_DELIVERY_MODE_EXTINT	= 7,
};

#endif /* _ASM_X86_APICDEF_H */
