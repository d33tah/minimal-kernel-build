/* SPDX-License-Identifier: GPL-2.0 */
/* Trimmed msr-index.h - only defines used by minimal kernel */
#ifndef _ASM_X86_MSR_INDEX_H
#define _ASM_X86_MSR_INDEX_H

#include <linux/bits.h>

/* AMD64 MSRs */
#define MSR_EFER		0xc0000080

/* EFER bits - EFER_NX, MSR_IA32_PRED_CMD, PRED_CMD_IBPB, SPEC_CTRL_SSBD removed - never used */
#define _EFER_NX		11

/* SPEC_CTRL_SSBD_SHIFT still used by spec-ctrl.h */
#define SPEC_CTRL_SSBD_SHIFT		2

/* MSR_IA32_FLUSH_CMD, L1D_FLUSH removed - never used */

/* SYSENTER MSRs */
#define MSR_IA32_SYSENTER_CS		0x00000174
#define MSR_IA32_SYSENTER_ESP		0x00000175
#define MSR_IA32_SYSENTER_EIP		0x00000176

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

/* CET (MSR_IA32_S_CET, CET_ENDBR_EN) removed - unused */

/* AMD specific */
#define MSR_AMD64_LS_CFG		0xc0011020
#define MSR_AMD64_VIRT_SPEC_CTRL	0xc001011f
#define MSR_K7_HWCR			0xc0010015
/* MSR_K8_INT_PENDING_MSG, K8_INTP_C1E_ACTIVE_MASK removed - never used */

/* VIA specific */
#define MSR_VIA_FCR			0x00001107

#endif /* _ASM_X86_MSR_INDEX_H */
