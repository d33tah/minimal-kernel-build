#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/stdarg.h>
#include <linux/limits.h>
#include <linux/const.h>

/* Inlined from align.h */
#define ALIGN(x, a)		__ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#define __ALIGN_MASK(x, mask)	__ALIGN_KERNEL_MASK((x), (mask))
#define IS_ALIGNED(x, a)		(((x) & ((typeof(x))(a) - 1)) == 0)
#include <linux/linkage.h>
#include <linux/stddef.h>
#include <linux/compiler.h>
#include <linux/container_of.h>
#include <linux/bitops.h>
#include <linux/kstrtox.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/panic.h>
#include <linux/printk.h>
#include <linux/build_bug.h>
#define _RET_IP_		(unsigned long)__builtin_return_address(0)
#define _THIS_IP_  ({ __label__ __here; __here: (unsigned long)&&__here; })
#include <asm/byteorder.h>

#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

#define READ			0
#define WRITE			1

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))


#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))

#define lower_32_bits(n) ((u32)((n) & 0xffffffff))


struct completion;


# define might_resched() do { } while (0)
# define might_sleep() do { might_resched(); } while (0)

#define might_sleep_if(cond) do { if (cond) might_sleep(); } while (0)

static inline void might_fault(void) { }

void do_exit(long error_code) __noreturn;

extern __printf(2, 3) int sprintf(char *buf, const char * fmt, ...);
extern __printf(3, 4)
int snprintf(char *buf, size_t size, const char *fmt, ...);
extern __printf(3, 0)
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
extern __printf(3, 0)
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
extern __printf(2, 0) __malloc
char *kvasprintf(gfp_t gfp, const char *fmt, va_list args);
extern __printf(2, 0)
const char *kvasprintf_const(gfp_t gfp, const char *fmt, va_list args);

extern void bust_spinlocks(int yes);


extern enum system_states {
	SYSTEM_BOOTING,
	SYSTEM_SCHEDULING,
	SYSTEM_FREEING_INITMEM,
	SYSTEM_RUNNING,
} system_state;

extern const char hex_asc_upper[];
/* Removed: hex_to_bin, hex2bin, bin2hex, mac_pton - never called */
/* Removed: tracing_off - empty stub, callers removed (TRACING off) */

#endif
