/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal APIC definitions - only actually used symbols */
#ifndef _ASM_X86_APICDEF_H
#define _ASM_X86_APICDEF_H

/* Only keeping definitions that are actually used */

#define	APIC_DEFAULT_PHYS_BASE		0xfee00000

#define	APIC_ID		0x20
#define	APIC_LVR	0x30
#define		APIC_LVR_DIRECTED_EOI	(1 << 24)
/* GET_APIC_VERSION, GET_APIC_MAXLVT, APIC_INTEGRATED, APIC_XAPIC, APIC_EXT_SPACE removed - unused */

#define	APIC_TASKPRI	0x80
#define	APIC_EOI	0xB0
#define	APIC_LDR	0xD0
#define	APIC_SPIV	0xF0
/* APIC_SPIV_APIC_ENABLED removed - unused */
#define	APIC_ISR	0x100
#define	APIC_ISR_NR     0x8
#define	APIC_TMR	0x180
#define	APIC_IRR	0x200
#define	APIC_ESR	0x280
#define	APIC_ICR	0x300
#define		APIC_ICR_BUSY		0x01000
#define	APIC_ICR2	0x310
#define	APIC_LVTT	0x320
#define	APIC_LVTTHMR	0x330
/* APIC_LVTPC removed - unused */
#define	APIC_LVT0	0x350
#define	APIC_LVT1	0x360
/* APIC_LVTERR, APIC_TMICT, APIC_TMCCT, APIC_TDCR removed - unused */
#define	APIC_EFEAT	0x400

#define MAX_LOCAL_APIC 256

#define BAD_APICID 0xFFu

enum apic_delivery_modes {
	APIC_DELIVERY_MODE_FIXED	= 0,
};

#endif /* _ASM_X86_APICDEF_H */
