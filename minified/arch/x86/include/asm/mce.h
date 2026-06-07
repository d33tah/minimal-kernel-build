/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal MCE header - CONFIG_X86_MCE is not set */
#ifndef _ASM_X86_MCE_H
#define _ASM_X86_MCE_H

#include <uapi/asm/mce.h>

/* Minimal stubs - MCE is disabled */
struct cpuinfo_x86;
struct cper_ia_proc_ctx;
struct pt_regs;

/* Only keeping functions that are actually called */
static inline int mcheck_init(void) { return 0; }
static inline void mcheck_cpu_init(struct cpuinfo_x86 *c) {}
static inline void mcheck_cpu_clear(struct cpuinfo_x86 *c) {}
/* Removed unused: enable_copy_mc_fragile, apei_smca_report_x86_error,
   mce_intel_feature_*, cmci_*, mce_threshold_*, amd_mce_*, mce_hygon_* */

#endif /* _ASM_X86_MCE_H */
