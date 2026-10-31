
#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

#define ASM_UD2 ".byte 0x0f, 0x0b"

#define _BUG_FLAGS(ins, flags, extra) asm volatile(ins)

#define BUG()                               \
	do {                                \
		_BUG_FLAGS(ASM_UD2, 0, ""); \
		__builtin_unreachable();    \
	} while (0)

/* --- 2025-12-07 10:25 --- Inlined asm-generic/bug.h content */

#ifndef __ASSEMBLY__

#define BUG_ON(condition)                \
	do {                             \
		if (unlikely(condition)) \
			BUG();           \
	} while (0)

#define WARN_ON(condition)                         \
	({                                         \
		int __ret_warn_on = !!(condition); \
		unlikely(__ret_warn_on);           \
	})

#ifndef WARN
#define WARN(condition, format...)                 \
	({                                         \
		int __ret_warn_on = !!(condition); \
		unlikely(__ret_warn_on);           \
	})
#endif

#define WARN_ON_ONCE(condition) WARN_ON(condition)
#define WARN_ONCE(condition, format...) WARN(condition, format)

#endif /* __ASSEMBLY__ */

#endif
