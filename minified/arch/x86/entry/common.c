
#include <linux/entry-common.h>

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

#include <asm/syscall.h>

/* inlined from asm/irq_stack.h */
#define run_irq_on_irqstack_cond(func, regs, vector) \
	{                                            \
		irq_enter_rcu();                     \
		func(regs, vector);                  \
		irq_exit_rcu();                      \
	}

static __always_inline int syscall_32_enter(struct pt_regs *regs)
{
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

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}
