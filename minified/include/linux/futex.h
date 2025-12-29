#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#include <linux/ktime.h>
#include <linux/types.h>

/* Minimal futex support - only defines/inlines actually used */
#define FUTEX_WAKE		1
#define FUTEX_TID_MASK		0x3fffffff

static inline long do_futex(u32 __user *uaddr, int op, u32 val,
			    ktime_t *timeout, u32 __user *uaddr2,
			    u32 val2, u32 val3)
{
	return -EINVAL;
}

#endif
