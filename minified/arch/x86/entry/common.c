
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/entry-common.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
/* linux/export.h removed - no EXPORT_SYMBOL */
/* Inlined from nospec.h */
#include <linux/compiler.h>
#include <asm/barrier.h>

struct task_struct;

#ifndef array_index_mask_nospec
static inline unsigned long array_index_mask_nospec(unsigned long index,
						    unsigned long size)
{
	OPTIMIZER_HIDE_VAR(index);
	return ~(long)(index | (size - 1UL - index)) >> (BITS_PER_LONG - 1);
}
#endif

#define array_index_nospec(index, size)                                \
	({                                                             \
		typeof(index) _i = (index);                            \
		typeof(size) _s = (size);                              \
		unsigned long _mask = array_index_mask_nospec(_i, _s); \
                                                                       \
		BUILD_BUG_ON(sizeof(_i) > sizeof(long));               \
		BUILD_BUG_ON(sizeof(_s) > sizeof(long));               \
                                                                       \
		(typeof(_i))(_i & _mask);                              \
	})
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/desc.h>
#include <asm/traps.h>
#include <asm/vdso.h>
#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#include <asm/nospec-branch.h>
#include <asm/io_bitmap.h>
#include <asm/syscall.h>
#include <asm/irq_stack.h>

static __always_inline int syscall_32_enter(struct pt_regs *regs)
{
	/* IA32_EMULATION disabled - skip TS_COMPAT */
	return (int)regs->orig_ax;
}

static __always_inline void do_syscall_32_irqs_on(struct pt_regs *regs, int nr)
{
	unsigned int unr = nr;

	if (likely(unr < IA32_NR_syscalls)) {
		unr = array_index_nospec(unr, IA32_NR_syscalls);
		regs->ax = ia32_sys_call_table[unr](regs);
	} else if (nr != -1) {
		regs->ax = __ia32_sys_ni_syscall(regs);
	}
}

__visible noinstr void do_int80_syscall_32(struct pt_regs *regs)
{
	int nr = syscall_32_enter(regs);

	nr = syscall_enter_from_user_mode(regs, nr);

	do_syscall_32_irqs_on(regs, nr);

	syscall_exit_to_user_mode(regs);
}

/* do_fast_syscall_32, do_SYSENTER_32 removed - init uses INT $0x80 only */

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}
