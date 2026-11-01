/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_PROCESSOR_FLAGS_H
#define _UAPI_ASM_X86_PROCESSOR_FLAGS_H

/* Various flags defined: can be included from assembler. */

#include <linux/const.h>

/*
 * EFLAGS bits
 */
#define X86_EFLAGS_CF_BIT 0 /* Carry Flag */
#define X86_EFLAGS_CF _BITUL(X86_EFLAGS_CF_BIT)
#define X86_EFLAGS_FIXED_BIT 1 /* Bit 1 - always on */
#define X86_EFLAGS_FIXED _BITUL(X86_EFLAGS_FIXED_BIT)
#define X86_EFLAGS_TF_BIT 8 /* Trap Flag */
#define X86_EFLAGS_TF _BITUL(X86_EFLAGS_TF_BIT)
#define X86_EFLAGS_IF_BIT 9 /* Interrupt Flag */
#define X86_EFLAGS_IF _BITUL(X86_EFLAGS_IF_BIT)
#define X86_EFLAGS_AC_BIT 18 /* Alignment Check/Access Control */
#define X86_EFLAGS_AC _BITUL(X86_EFLAGS_AC_BIT)
#define X86_EFLAGS_ID_BIT 21 /* CPUID detection */
#define X86_EFLAGS_ID _BITUL(X86_EFLAGS_ID_BIT)

/*
 * Basic CPU control in CR0
 */
#define X86_CR0_PE_BIT 0 /* Protection Enable */
#define X86_CR0_PE _BITUL(X86_CR0_PE_BIT)
#define X86_CR0_MP_BIT 1 /* Monitor Coprocessor */
#define X86_CR0_MP _BITUL(X86_CR0_MP_BIT)
#define X86_CR0_TS_BIT 3 /* Task Switched */
#define X86_CR0_TS _BITUL(X86_CR0_TS_BIT)
#define X86_CR0_ET_BIT 4 /* Extension Type */
#define X86_CR0_ET _BITUL(X86_CR0_ET_BIT)
#define X86_CR0_NE_BIT 5 /* Numeric Error */
#define X86_CR0_NE _BITUL(X86_CR0_NE_BIT)
#define X86_CR0_WP_BIT 16 /* Write Protect */
#define X86_CR0_WP _BITUL(X86_CR0_WP_BIT)
#define X86_CR0_AM_BIT 18 /* Alignment Mask */
#define X86_CR0_AM _BITUL(X86_CR0_AM_BIT)
#define X86_CR0_PG_BIT 31 /* Paging */
#define X86_CR0_PG _BITUL(X86_CR0_PG_BIT)

/*
 * Intel CPU features in CR4
 */
#define X86_CR4_VME_BIT 0 /* enable vm86 extensions */
#define X86_CR4_VME _BITUL(X86_CR4_VME_BIT)
#define X86_CR4_PVI_BIT 1 /* virtual interrupts flag enable */
#define X86_CR4_PVI _BITUL(X86_CR4_PVI_BIT)
#define X86_CR4_TSD_BIT 2 /* disable time stamp at ipl 3 */
#define X86_CR4_TSD _BITUL(X86_CR4_TSD_BIT)
#define X86_CR4_DE_BIT 3 /* enable debugging extensions */
#define X86_CR4_DE _BITUL(X86_CR4_DE_BIT)
#define X86_CR4_PSE_BIT 4 /* enable page size extensions */
#define X86_CR4_PSE _BITUL(X86_CR4_PSE_BIT)
#define X86_CR4_PGE_BIT 7 /* enable global pages */
#define X86_CR4_PGE _BITUL(X86_CR4_PGE_BIT)
#define X86_CR4_PCE_BIT 8 /* enable performance counters at ipl 3 */
#define X86_CR4_PCE _BITUL(X86_CR4_PCE_BIT)
#define X86_CR4_UMIP_BIT 11 /* enable UMIP support */
#define X86_CR4_UMIP _BITUL(X86_CR4_UMIP_BIT)
#define X86_CR4_FSGSBASE_BIT 16 /* enable RDWRFSGS support */
#define X86_CR4_FSGSBASE _BITUL(X86_CR4_FSGSBASE_BIT)
#define X86_CR4_PCIDE_BIT 17 /* enable PCID support */
#define X86_CR4_PCIDE _BITUL(X86_CR4_PCIDE_BIT)
#define X86_CR4_SMEP_BIT 20 /* enable SMEP support */
#define X86_CR4_SMEP _BITUL(X86_CR4_SMEP_BIT)

#define CR0_STATE                                                         \
	(X86_CR0_PE | X86_CR0_MP | X86_CR0_ET | X86_CR0_NE | X86_CR0_WP | \
	 X86_CR0_AM | X86_CR0_PG)

#endif /* _UAPI_ASM_X86_PROCESSOR_FLAGS_H */
