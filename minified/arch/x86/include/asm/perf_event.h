/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PERF_EVENT_H
#define _ASM_X86_PERF_EVENT_H

/* Minimal stub for perf events - all functionality stubbed */

#include <linux/static_call.h>
#include <asm/stacktrace.h>

/* Minimal structures needed for compilation */
struct perf_guest_switch_msr {
	unsigned msr;
	u64 host, guest;
};

struct x86_pmu_lbr {
	unsigned int nr;
	unsigned int from;
	unsigned int to;
	unsigned int info;
};

struct x86_pmu_capability {
	int version;
	int num_counters_gp;
	int num_counters_fixed;
	int bit_width_gp;
	int bit_width_fixed;
	unsigned int events_mask;
	int events_mask_len;
};

struct x86_perf_regs {
	struct pt_regs regs;
	u64 *xmm_regs;
};

/* Function declarations - implemented as stubs in kernel/events/stubs.c */
extern void perf_clear_dirty_counters(void);

/* Inline stubs */
static inline void perf_get_x86_pmu_capability(struct x86_pmu_capability *cap) { }
static inline void perf_check_microcode(void) { }
static inline int x86_perf_rdpmc_index(struct perf_event *event) { return -ENOSYS; }
static inline struct perf_guest_switch_msr *perf_guest_get_msrs(int *nr) { *nr = 0; return NULL; }
static inline int x86_perf_get_lbr(struct x86_pmu_lbr *lbr) { return -EOPNOTSUPP; }
static inline void intel_pt_handle_vmx(int on) { }
static inline void amd_pmu_enable_virt(void) { }
static inline void amd_pmu_disable_virt(void) { }
static inline unsigned long perf_instruction_pointer(struct pt_regs *regs) { return 0; }
static inline unsigned long perf_misc_flags(struct pt_regs *regs) { return 0; }

#define perf_misc_flags(regs) perf_misc_flags(regs)

/* Minimal arch-specific macros */
#define perf_arch_fetch_caller_regs(regs, __ip) do { \
	(regs)->ip = (__ip); \
	(regs)->sp = (unsigned long)__builtin_frame_address(0); \
	(regs)->cs = __KERNEL_CS; \
	(regs)->flags = 0; \
} while (0)

#define arch_perf_out_copy_user copy_from_user_nmi

#endif /* _ASM_X86_PERF_EVENT_H */
