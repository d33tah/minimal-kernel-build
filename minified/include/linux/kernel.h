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
/* asm/byteorder.h inlined */
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif
#define __swab16(x) (__u16)__builtin_bswap16((__u16)(x))
#define __swab32(x) (__u32)__builtin_bswap32((__u32)(x))
#define __swab64(x) (__u64)__builtin_bswap64((__u64)(x))
#define __cpu_to_le64(x) ((__force __le64)(__u64)(x))
#define __le64_to_cpu(x) ((__force __u64)(__le64)(x))
#define __cpu_to_le32(x) ((__force __le32)(__u32)(x))
#define __le32_to_cpu(x) ((__force __u32)(__le32)(x))
#define __cpu_to_le16(x) ((__force __le16)(__u16)(x))
#define __le16_to_cpu(x) ((__force __u16)(__le16)(x))
#define __cpu_to_be64(x) ((__force __be64)__swab64((x)))
#define __be64_to_cpu(x) __swab64((__force __u64)(__be64)(x))
#define __cpu_to_be32(x) ((__force __be32)__swab32((x)))
#define __be32_to_cpu(x) __swab32((__force __u32)(__be32)(x))
#define __cpu_to_be16(x) ((__force __be16)__swab16((x)))
#define __be16_to_cpu(x) __swab16((__force __u16)(__be16)(x))
#define cpu_to_le64 __cpu_to_le64
#define le64_to_cpu __le64_to_cpu
#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu
#define cpu_to_le16 __cpu_to_le16
#define le16_to_cpu __le16_to_cpu
#define cpu_to_be64 __cpu_to_be64
#define be64_to_cpu __be64_to_cpu
#define cpu_to_be32 __cpu_to_be32
#define be32_to_cpu __be32_to_cpu
#define cpu_to_be16 __cpu_to_be16
#define be16_to_cpu __be16_to_cpu
#undef ntohl
#undef ntohs
#undef htonl
#undef htons
#define ___htonl(x) __cpu_to_be32(x)
#define ___htons(x) __cpu_to_be16(x)
#define ___ntohl(x) __be32_to_cpu(x)
#define ___ntohs(x) __be16_to_cpu(x)
#define htonl(x) ___htonl(x)
#define ntohl(x) ___ntohl(x)
#define htons(x) ___htons(x)
#define ntohs(x) ___ntohs(x)

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
