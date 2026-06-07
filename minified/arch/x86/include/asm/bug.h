 
#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

#include <linux/stringify.h>
#include <linux/objtool.h>

 
#define ASM_UD2		".byte 0x0f, 0x0b"
#define INSN_UD2	0x0b0f
#define LEN_UD2		2


#define _BUG_FLAGS(ins, flags, extra)  asm volatile(ins)


#define HAVE_ARCH_BUG
#define BUG()							\
do {								\
	_BUG_FLAGS(ASM_UD2, 0, "");				\
	__builtin_unreachable();				\
} while (0)

#define __WARN_FLAGS(flags)					\
do {								\
	__auto_type __flags = BUGFLAG_WARNING|(flags);		\
	_BUG_FLAGS(ASM_UD2, __flags, ASM_REACHABLE);		\
} while (0)

/* --- 2025-12-07 10:25 --- Inlined asm-generic/bug.h content */
#include <linux/compiler.h>

#define CUT_HERE		"------------[ cut here ]------------\n"

#ifndef __ASSEMBLY__
#include <linux/panic.h>
#include <linux/printk.h>

struct warn_args;
struct pt_regs;

void __warn(const char *file, int line, void *caller, unsigned taint,
	    struct pt_regs *regs, struct warn_args *args);

#ifndef HAVE_ARCH_BUG
#define BUG() do {} while (1)
#endif

#ifndef HAVE_ARCH_BUG_ON
#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while (0)
#endif

#ifndef HAVE_ARCH_WARN_ON
#define WARN_ON(condition) ({						\
	int __ret_warn_on = !!(condition);				\
	unlikely(__ret_warn_on);					\
})
#endif

#ifndef WARN
#define WARN(condition, format...) ({					\
	int __ret_warn_on = !!(condition);				\
	no_printk(format);						\
	unlikely(__ret_warn_on);					\
})
#endif

#define WARN_ON_ONCE(condition) WARN_ON(condition)
#define WARN_ONCE(condition, format...) WARN(condition, format)
#define WARN_TAINT(condition, taint, format...) WARN(condition, format)
#define WARN_TAINT_ONCE(condition, taint, format...) WARN(condition, format)

# define WARN_ON_SMP(x)			({0;})

# define WARN_ON_FUNCTION_MISMATCH(x, fn) WARN_ON_ONCE((x) != (fn))

#endif /* __ASSEMBLY__ */

#endif
