 
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

#include <linux/compiler.h>

#ifndef __ASSEMBLY__
#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H
#include <linux/compiler_attributes.h>
__printf(1, 2)
void panic(const char *fmt, ...) __noreturn __cold;
#define PANIC_CPU_INVALID	-1
#define TAINT_USER			6
#define TAINT_DIE			7
enum lockdep_ok {
	LOCKDEP_STILL_OK,
	LOCKDEP_NOW_UNRELIABLE,
};
extern void add_taint(unsigned flag, enum lockdep_ok);
#endif /* _LINUX_PANIC_H */
#include <linux/printk.h>

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

#endif /* __ASSEMBLY__ */

#endif
