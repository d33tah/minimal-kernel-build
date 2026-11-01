/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2008 Intel Corporation
 * Author: Matthew Wilcox <willy@linux.intel.com>
 *
 * Please see kernel/locking/semaphore.c for documentation of these functions
 */
#ifndef __LINUX_SEMAPHORE_H
#define __LINUX_SEMAPHORE_H

#include <linux/spinlock.h>

/* Please don't access any members of this structure directly */
struct semaphore {
	raw_spinlock_t lock;
	unsigned int count;
};

#define __SEMAPHORE_INITIALIZER(name, n)                       \
	{                                                      \
		.lock = __RAW_SPIN_LOCK_UNLOCKED((name).lock), \
		.count = n,                                    \
	}

#define DEFINE_SEMAPHORE(name) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)

extern void down(struct semaphore *sem);
extern void up(struct semaphore *sem);

#endif /* __LINUX_SEMAPHORE_H */
