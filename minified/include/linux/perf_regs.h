/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PERF_REGS_H
#define _LINUX_PERF_REGS_H

#include <linux/sched/task_stack.h>

struct perf_regs {
	__u64		abi;
	struct pt_regs	*regs;
};

#include <asm/perf_regs.h>

#ifndef PERF_REG_EXTENDED_MASK
#define PERF_REG_EXTENDED_MASK	0
#endif

u64 perf_reg_value(struct pt_regs *regs, int idx);
int perf_reg_validate(u64 mask);
u64 perf_reg_abi(struct task_struct *task);
void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs);
#endif /* _LINUX_PERF_REGS_H */
