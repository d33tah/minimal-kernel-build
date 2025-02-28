// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/perf_event.h>
#include <linux/bug.h>
#include <linux/stddef.h>
#include <asm/perf_regs.h>
#include <asm/ptrace.h>

#define PERF_REG_X86_MAX PERF_REG_X86_32_MAX

#define PT_REGS_OFFSET(id, r) [id] = offsetof(struct pt_regs, r)

static unsigned int pt_regs_offset[PERF_REG_X86_MAX] = {
	PT_REGS_OFFSET(PERF_REG_X86_AX, ax),
	PT_REGS_OFFSET(PERF_REG_X86_BX, bx),
	PT_REGS_OFFSET(PERF_REG_X86_CX, cx),
	PT_REGS_OFFSET(PERF_REG_X86_DX, dx),
	PT_REGS_OFFSET(PERF_REG_X86_SI, si),
	PT_REGS_OFFSET(PERF_REG_X86_DI, di),
	PT_REGS_OFFSET(PERF_REG_X86_BP, bp),
	PT_REGS_OFFSET(PERF_REG_X86_SP, sp),
	PT_REGS_OFFSET(PERF_REG_X86_IP, ip),
	PT_REGS_OFFSET(PERF_REG_X86_FLAGS, flags),
	PT_REGS_OFFSET(PERF_REG_X86_CS, cs),
	PT_REGS_OFFSET(PERF_REG_X86_SS, ss),
	PT_REGS_OFFSET(PERF_REG_X86_DS, ds),
	PT_REGS_OFFSET(PERF_REG_X86_ES, es),
	PT_REGS_OFFSET(PERF_REG_X86_FS, fs),
	PT_REGS_OFFSET(PERF_REG_X86_GS, gs),
};

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	struct x86_perf_regs *perf_regs;

	if (idx >= PERF_REG_X86_XMM0 && idx < PERF_REG_X86_XMM_MAX) {
		perf_regs = container_of(regs, struct x86_perf_regs, regs);
		if (!perf_regs->xmm_regs)
			return 0;
		return perf_regs->xmm_regs[idx - PERF_REG_X86_XMM0];
	}

	if (WARN_ON_ONCE(idx >= ARRAY_SIZE(pt_regs_offset)))
		return 0;

	return regs_get_register(regs, pt_regs_offset[idx]);
}

#define PERF_REG_X86_RESERVED	(((1ULL << PERF_REG_X86_XMM0) - 1) & \
				 ~((1ULL << PERF_REG_X86_MAX) - 1))

#define REG_NOSUPPORT ((1ULL << PERF_REG_X86_R8) | \
		       (1ULL << PERF_REG_X86_R9) | \
		       (1ULL << PERF_REG_X86_R10) | \
		       (1ULL << PERF_REG_X86_R11) | \
		       (1ULL << PERF_REG_X86_R12) | \
		       (1ULL << PERF_REG_X86_R13) | \
		       (1ULL << PERF_REG_X86_R14) | \
		       (1ULL << PERF_REG_X86_R15))

int perf_reg_validate(u64 mask)
{
	if (!mask || (mask & (REG_NOSUPPORT | PERF_REG_X86_RESERVED)))
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	return PERF_SAMPLE_REGS_ABI_32;
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}
