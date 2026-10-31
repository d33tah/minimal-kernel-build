#ifndef _LINUX_KTIME_H
#define _LINUX_KTIME_H

#include <linux/time.h>
#include <linux/jiffies.h>
#include <asm/bug.h>

typedef s64 ktime_t;

/* BITS_PER_LONG == 32 */

/* Inlined from vdso/ktime.h */

#include <linux/timekeeping.h>

#endif
