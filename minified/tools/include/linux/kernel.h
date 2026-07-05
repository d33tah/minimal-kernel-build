
#ifndef __TOOLS_LINUX_KERNEL_H
#define __TOOLS_LINUX_KERNEL_H

#include <stdarg.h>
#include <stddef.h>
#include <assert.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/math.h>
#include <endian.h>
#include <byteswap.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#endif

#endif
