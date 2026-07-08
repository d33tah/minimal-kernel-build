/* SPDX-License-Identifier: GPL-2.0 */
/* Trimmed msr-index.h - only defines used by minimal kernel */
#ifndef _ASM_X86_MSR_INDEX_H
#define _ASM_X86_MSR_INDEX_H

#include <linux/bits.h>

/* AMD64 MSRs */
#define MSR_EFER		0xc0000080

/* EFER bits */
#define _EFER_NX		11
/* _EFER_LME, _EFER_SCE, _EFER_LMA, _EFER_SVME, _EFER_LMSLE, _EFER_FFXSR - unused */


/* SYSENTER MSRs */
#define MSR_IA32_SYSENTER_CS		0x00000174
#define MSR_IA32_SYSENTER_ESP		0x00000175
#define MSR_IA32_SYSENTER_EIP		0x00000176

/* MISC_ENABLE */
#define MSR_IA32_MISC_ENABLE		0x000001a0

/* AMD specific */
#define MSR_K7_HWCR			0xc0010015

/* VIA specific */
#define MSR_VIA_FCR			0x00001107

#endif /* _ASM_X86_MSR_INDEX_H */
