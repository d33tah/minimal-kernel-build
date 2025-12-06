/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal MCE header - CONFIG_X86_MCE is not set */
#ifndef _ASM_X86_MCE_H
#define _ASM_X86_MCE_H

#include <uapi/asm/mce.h>

/* Minimal stubs - MCE is disabled */
struct cpuinfo_x86;
struct cper_ia_proc_ctx;
struct pt_regs;

static inline void enable_copy_mc_fragile(void) {}
static inline int mcheck_init(void) { return 0; }
static inline void mcheck_cpu_init(struct cpuinfo_x86 *c) {}
static inline void mcheck_cpu_clear(struct cpuinfo_x86 *c) {}
static inline int apei_smca_report_x86_error(struct cper_ia_proc_ctx *ctx_info,
					     u64 lapic_id) { return -EINVAL; }

static inline void mce_intel_feature_init(struct cpuinfo_x86 *c) {}
static inline void mce_intel_feature_clear(struct cpuinfo_x86 *c) {}
static inline void cmci_clear(void) {}
static inline void cmci_reenable(void) {}
static inline void cmci_rediscover(void) {}
static inline void cmci_recheck(void) {}

static inline int mce_threshold_create_device(unsigned int cpu) { return 0; }
static inline int mce_threshold_remove_device(unsigned int cpu) { return 0; }
static inline bool amd_mce_is_memory_error(struct mce *m) { return false; }
static inline void mce_amd_feature_init(struct cpuinfo_x86 *c) {}
static inline void mce_hygon_feature_init(struct cpuinfo_x86 *c) {}

#endif /* _ASM_X86_MCE_H */
