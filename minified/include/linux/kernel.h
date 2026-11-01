/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NOTE:
 *
 * This header has combined a lot of unrelated to each other stuff.
 * The process of splitting its content is in progress while keeping
 * backward compatibility. That's why it's highly recommended NOT to
 * include this header inside another header file, especially under
 * generic or architectural include/ directory.
 */
#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/limits.h>
#include <linux/const.h>

#define ALIGN(x, a) __ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a) __ALIGN_KERNEL((x) - ((a) - 1), (a))
#include <linux/linkage.h>
#include <linux/container_of.h>
#include <linux/bitops.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/panic.h>
#define _RET_IP_ (unsigned long)__builtin_return_address(0)

/**
 * REPEAT_BYTE - repeat the value @x multiple times as an unsigned long value
 * @x: value to repeat
 *
 * NOTE: @x is not checked for > 0xff; larger values produce odd results.
 */
#define REPEAT_BYTE(x) ((~0ul / 0xff) * (x))

/* generic data direction definitions */
#define READ 0
#define WRITE 1

/**
 * ARRAY_SIZE - get the number of elements in array @arr
 * @arr: array to be sized
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

/**
 * lower_32_bits - return bits 0-31 of a number
 * @n: the number we're accessing
 */
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))

void do_exit(long error_code) __noreturn;

#endif
