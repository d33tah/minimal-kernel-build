#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/limits.h>
#include <linux/const.h>

/* Inlined from align.h */
#define ALIGN(x, a) __ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a) __ALIGN_KERNEL((x) - ((a) - 1), (a))
#include <linux/linkage.h>
#include <linux/container_of.h>
#include <linux/bitops.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/panic.h>
#define _RET_IP_ (unsigned long)__builtin_return_address(0)

#define REPEAT_BYTE(x) ((~0ul / 0xff) * (x))

#define READ 0
#define WRITE 1

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#define lower_32_bits(n) ((u32)((n) & 0xffffffff))

void do_exit(long error_code) __noreturn;

/* Removed: hex_to_bin, hex2bin, bin2hex, mac_pton - never called */
/* Removed: tracing_off - empty stub, callers removed (TRACING off) */

#endif
