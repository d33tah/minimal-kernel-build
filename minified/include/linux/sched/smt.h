#ifndef _LINUX_SCHED_SMT_H
#define _LINUX_SCHED_SMT_H

#include <linux/static_key.h>

static inline bool sched_smt_active(void) { return false; }

void arch_smt_update(void);

#endif
