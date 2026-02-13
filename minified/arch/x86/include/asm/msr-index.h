/* SPDX-License-Identifier: GPL-2.0 */
/* Trimmed msr-index.h - only defines used by minimal kernel */
#ifndef _ASM_X86_MSR_INDEX_H
#define _ASM_X86_MSR_INDEX_H

#include <linux/bits.h>

/* AMD64 MSRs */
#define MSR_EFER		0xc0000080

#define _EFER_NX		11

/* SPEC_CTRL_SSBD_SHIFT still used by spec-ctrl.h */
#define SPEC_CTRL_SSBD_SHIFT		2

/* Debug control */
#define MSR_IA32_DEBUGCTLMSR		0x000001d9
#define DEBUGCTLMSR_BTF_SHIFT		1
#define DEBUGCTLMSR_BTF			(1UL <<  1)

/* MISC_ENABLE */
#define MSR_IA32_MISC_ENABLE		0x000001a0

/* MISC_FEATURES_ENABLES */
#define MSR_MISC_FEATURES_ENABLES	0x00000140
#define MSR_MISC_FEATURES_ENABLES_CPUID_FAULT_BIT	0
#define MSR_MISC_FEATURES_ENABLES_CPUID_FAULT		BIT_ULL(MSR_MISC_FEATURES_ENABLES_CPUID_FAULT_BIT)

/* AMD specific */
#define MSR_AMD64_LS_CFG		0xc0011020
#define MSR_AMD64_VIRT_SPEC_CTRL	0xc001011f
#define MSR_K7_HWCR			0xc0010015

/* VIA specific */
#define MSR_VIA_FCR			0x00001107

#endif /* _ASM_X86_MSR_INDEX_H */
