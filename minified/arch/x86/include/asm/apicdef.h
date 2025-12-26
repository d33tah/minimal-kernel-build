/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal APIC definitions - only actually used symbols */
#ifndef _ASM_X86_APICDEF_H
#define _ASM_X86_APICDEF_H

/* Only keeping definitions that are actually used */

#define	APIC_DEFAULT_PHYS_BASE		0xfee00000

#define	APIC_ID		0x20
#define	APIC_LVR	0x30
#define		APIC_LVR_MASK		0xFF00FF
#define		APIC_LVR_DIRECTED_EOI	(1 << 24)
#define		GET_APIC_VERSION(x)	((x) & 0xFFu)
#define		GET_APIC_MAXLVT(x)	(((x) >> 16) & 0xFFu)
#define		APIC_INTEGRATED(x)	((x) & 0xF0u)
#define		APIC_XAPIC(x)		((x) >= 0x14)
#define		APIC_EXT_SPACE(x)	((x) & 0x80000000)

#define	APIC_TASKPRI	0x80
#define		APIC_TPRI_MASK		0xFFu
#define	APIC_EOI	0xB0
#define	APIC_LDR	0xD0
#define	APIC_SPIV	0xF0
#define		APIC_SPIV_APIC_ENABLED		(1 << 8)
#define	APIC_ISR	0x100
#define	APIC_ISR_NR     0x8
#define	APIC_TMR	0x180
#define	APIC_IRR	0x200
#define	APIC_ESR	0x280
#define 	APIC_LVTCMCI	0x2f0
#define	APIC_ICR	0x300
#define		APIC_ICR_BUSY		0x01000
#define		APIC_VECTOR_MASK	0x000FF
#define	APIC_ICR2	0x310
#define	APIC_LVTT	0x320
#define	APIC_LVTTHMR	0x330
#define	APIC_LVTPC	0x340
#define	APIC_LVT0	0x350
#define		APIC_LVT_MASKED			(1 << 16)
#define	APIC_LVT1	0x360
#define	APIC_LVTERR	0x370
#define	APIC_TMICT	0x380
#define	APIC_TMCCT	0x390
#define	APIC_TDCR	0x3E0
#define	APIC_EFEAT	0x400

#define APIC_BASE (fix_to_virt(FIX_APIC_BASE))
#define MAX_LOCAL_APIC 256

/* Cluster destination mode macros */
#define XAPIC_DEST_CPUS_SHIFT	4
#define XAPIC_DEST_CPUS_MASK	((1u << XAPIC_DEST_CPUS_SHIFT) - 1)
#define XAPIC_DEST_CLUSTER_MASK	(XAPIC_DEST_CPUS_MASK << XAPIC_DEST_CPUS_SHIFT)
#define APIC_CLUSTER(apicid)	((apicid) & XAPIC_DEST_CLUSTER_MASK)

#define BAD_APICID 0xFFu

enum apic_delivery_modes {
	APIC_DELIVERY_MODE_FIXED	= 0,
};

#endif /* _ASM_X86_APICDEF_H */
