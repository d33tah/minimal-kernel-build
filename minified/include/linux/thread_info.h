/* SPDX-License-Identifier: GPL-2.0 */
/* thread_info.h: common low-level thread information accessors
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds
 */

#ifndef _LINUX_THREAD_INFO_H
#define _LINUX_THREAD_INFO_H

#include <asm/bug.h>

/*
 * For CONFIG_THREAD_INFO_IN_TASK kernels we need <asm/current.h> for the
 * definition of current, but for !CONFIG_THREAD_INFO_IN_TASK kernels,
 * including <asm/current.h> can cause a circular dependency on some platforms.
 */
#include <asm/current.h>
#define current_thread_info() ((struct thread_info *)current)

#include <asm/thread_info.h>

#define THREADINFO_GFP (GFP_KERNEL_ACCOUNT | __GFP_ZERO)

/*
 * flag set/clear/test wrappers
 * - pass TIF_xxxx constants to these functions
 */

static inline void set_ti_thread_flag(struct thread_info *ti, int flag)
{
	set_bit(flag, (unsigned long *)&ti->flags);
}

static inline void clear_ti_thread_flag(struct thread_info *ti, int flag)
{
	clear_bit(flag, (unsigned long *)&ti->flags);
}

static inline int test_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_bit(flag, (unsigned long *)&ti->flags);
}

static __always_inline unsigned long
read_ti_thread_flags(struct thread_info *ti)
{
	return READ_ONCE(ti->flags);
}

#define test_thread_flag(flag) test_ti_thread_flag(current_thread_info(), flag)
#define read_thread_flags() read_ti_thread_flags(current_thread_info())

#define tif_need_resched() test_thread_flag(TIF_NEED_RESCHED)

extern void __compiletime_error("copy source size is too small")
	__bad_copy_from(void);
extern void __compiletime_error("copy destination size is too small")
	__bad_copy_to(void);

static __always_inline __must_check bool
check_copy_size(const void *addr, size_t bytes, bool is_source)
{
	int sz = __builtin_object_size(addr, 0);
	if (unlikely(sz >= 0 && sz < bytes)) {
		if (__builtin_constant_p(bytes) && is_source)
			__bad_copy_from();
		else if (__builtin_constant_p(bytes))
			__bad_copy_to();
		return false;
	}
	if (WARN_ON_ONCE(bytes > INT_MAX))
		return false;
	return true;
}

#endif
