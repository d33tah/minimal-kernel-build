#ifndef _LINUX_TRACE_IRQFLAGS_H
#define _LINUX_TRACE_IRQFLAGS_H

#include <linux/typecheck.h>
#include <asm/irqflags.h>
#include <asm/percpu.h>

#ifndef ftrace_return_address0
# define ftrace_return_address0 __builtin_return_address(0)
#endif
#define CALLER_ADDR0 ((unsigned long)ftrace_return_address0)

struct task_struct;
static inline unsigned long ftrace_graph_ret_addr(struct task_struct *task, int *idx,
						  unsigned long ret, unsigned long *retp)
{
	return ret;
}
static inline bool is_ftrace_trampoline(unsigned long addr)
{
	return false;
}

  /* lockdep_softirqs_on/off removed - unused (empty no-op stubs) */
  static inline void lockdep_hardirqs_on_prepare(void) { }
  static inline void lockdep_hardirqs_on(unsigned long ip) { }
  static inline void lockdep_hardirqs_off(unsigned long ip) { }

# define trace_hardirqs_off()			do { } while (0)
# define lockdep_hardirqs_enabled()		0
# define lockdep_hardirq_enter()		do { } while (0)
# define lockdep_hardirq_exit()			do { } while (0)

# define stop_critical_timings() do { } while (0)
# define start_critical_timings() do { } while (0)

#define raw_check_bogus_irq_restore() do { } while (0)

#define raw_local_irq_disable()		arch_local_irq_disable()
#define raw_local_irq_enable()		arch_local_irq_enable()
#define raw_local_irq_save(flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = arch_local_irq_save();		\
	} while (0)
#define raw_local_irq_restore(flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		raw_check_bogus_irq_restore();		\
		arch_local_irq_restore(flags);		\
	} while (0)
#define raw_local_save_flags(flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = arch_local_save_flags();	\
	} while (0)
#define raw_irqs_disabled_flags(flags)			\
	({						\
		typecheck(unsigned long, flags);	\
		arch_irqs_disabled_flags(flags);	\
	})
#define raw_irqs_disabled()		(arch_irqs_disabled())
#define raw_safe_halt()			arch_safe_halt()


#define local_irq_enable()	do { raw_local_irq_enable(); } while (0)
#define local_irq_disable()	do { raw_local_irq_disable(); } while (0)
#define local_irq_save(flags)	do { raw_local_irq_save(flags); } while (0)
#define local_irq_restore(flags) do { raw_local_irq_restore(flags); } while (0)
#define safe_halt()		do { raw_safe_halt(); } while (0)


#define local_save_flags(flags)	raw_local_save_flags(flags)

#define irqs_disabled()					\
	({						\
		unsigned long _flags;			\
		raw_local_save_flags(_flags);		\
		raw_irqs_disabled_flags(_flags);	\
	})

#define irqs_disabled_flags(flags) raw_irqs_disabled_flags(flags)

#endif
