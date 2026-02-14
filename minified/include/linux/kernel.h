#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/stdarg.h>
#include <linux/limits.h>
#include <linux/const.h>

#define ALIGN(x, a)		__ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#define IS_ALIGNED(x, a)		(((x) & ((typeof(x))(a) - 1)) == 0)
#include <linux/linkage.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/container_of.h>
#include <linux/bitops.h>
int __must_check kstrtoull(const char *s, unsigned int base, unsigned long long *res);
int __must_check kstrtouint(const char *s, unsigned int base, unsigned int *res);
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/typecheck.h>
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
#include <linux/build_bug.h>
#include <linux/static_call_types.h>
#define _RET_IP_		(unsigned long)__builtin_return_address(0)
#define _THIS_IP_  ({ __label__ __here; __here: (unsigned long)&&__here; })
#include <asm/byteorder.h>

#include <linux/const.h>

#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

#define READ			0
#define WRITE			1

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#define lower_32_bits(n) ((u32)((n) & 0xffffffff))

#define might_sleep() do { } while (0)
#define might_sleep_if(cond) do { } while (0)

void do_exit(long error_code) __noreturn;

extern __printf(2, 3) int sprintf(char *buf, const char * fmt, ...);
extern __printf(2, 0) int vsprintf(char *buf, const char *, va_list);
extern __printf(3, 4)
int snprintf(char *buf, size_t size, const char *fmt, ...);
extern __printf(3, 0)
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
extern __printf(3, 4)
extern __printf(3, 0)
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
extern __printf(2, 3) __malloc
char *kasprintf(gfp_t gfp, const char *fmt, ...);
extern __printf(2, 0) __malloc
char *kvasprintf(gfp_t gfp, const char *fmt, va_list args);

extern void bust_spinlocks(int yes);

extern enum system_states {
	SYSTEM_BOOTING,
	SYSTEM_SCHEDULING,
	SYSTEM_FREEING_INITMEM,
	SYSTEM_RUNNING,
	SYSTEM_HALT,
	SYSTEM_POWER_OFF,
	SYSTEM_RESTART,
	SYSTEM_SUSPEND,
} system_state;

/* hex_asc_upper moved from lib/hexdump.c */
static const char hex_asc_upper[] = "0123456789ABCDEF";

#endif
