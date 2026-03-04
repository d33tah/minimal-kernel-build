#ifndef _LINUX_BUG_H
#define _LINUX_BUG_H

#include <asm/bug.h>
#include <linux/build_bug.h>

enum bug_trap_type {
	BUG_TRAP_TYPE_NONE = 0,
	BUG_TRAP_TYPE_WARN = 1,
	BUG_TRAP_TYPE_BUG = 2,
};

struct pt_regs;

static inline enum bug_trap_type report_bug(unsigned long bug_addr,
					    struct pt_regs *regs)
{
	return BUG_TRAP_TYPE_BUG;
}

#define VM_BUG_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_BUG_ON_PAGE(cond, page) VM_BUG_ON(cond)
#define VM_BUG_ON_MM(cond, mm) VM_BUG_ON(cond)
#define VM_WARN_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_ONCE(cond) BUILD_BUG_ON_INVALID(cond)

#endif
