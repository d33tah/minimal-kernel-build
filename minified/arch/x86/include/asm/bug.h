/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

/*
 * Despite that some emulators terminate on UD2, we use it for WARN().
 */
#define ASM_UD2 ".byte 0x0f, 0x0b"

#define _BUG_FLAGS(ins, flags, extra) asm volatile(ins)

#define BUG()                               \
	do {                                \
		_BUG_FLAGS(ASM_UD2, 0, ""); \
		__builtin_unreachable();    \
	} while (0)

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

#define WARN(condition, format...)                 \
	({                                         \
		int __ret_warn_on = !!(condition); \
		unlikely(__ret_warn_on);           \
	})

#define WARN_ON_ONCE(condition) WARN_ON(condition)
#define WARN_ONCE(condition, format...) WARN(condition, format)

#endif /* _ASM_X86_BUG_H */
