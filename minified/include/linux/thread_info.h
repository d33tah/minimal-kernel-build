
#ifndef _LINUX_THREAD_INFO_H
#define _LINUX_THREAD_INFO_H

#include <linux/limits.h>
#include <linux/bug.h>
/* time64.h inlined */
#include <linux/math64.h>
#ifndef NSEC_PER_SEC
#define NSEC_PER_MSEC	1000000L
#define NSEC_PER_SEC	1000000000L
typedef __s64 time64_t;
struct timespec64 {
	time64_t	tv_sec;
	long		tv_nsec;
};
#endif

#include <linux/errno.h>

#include <asm/current.h>
#define current_thread_info() ((struct thread_info *)current)

#include <linux/bitops.h>

enum syscall_work_bit {
	SYSCALL_WORK_BIT_SYSCALL_TRACE = 2,
	SYSCALL_WORK_BIT_SYSCALL_EMU = 3,
};

#include <asm/thread_info.h>

#ifdef __KERNEL__

#define THREADINFO_GFP		(GFP_KERNEL_ACCOUNT | __GFP_ZERO)

static inline void set_ti_thread_flag(struct thread_info *ti, int flag)
{
	set_bit(flag, (unsigned long *)&ti->flags);
}

static inline void clear_ti_thread_flag(struct thread_info *ti, int flag)
{
	clear_bit(flag, (unsigned long *)&ti->flags);
}

static inline int test_and_set_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_and_set_bit(flag, (unsigned long *)&ti->flags);
}

static inline int test_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_bit(flag, (unsigned long *)&ti->flags);
}

static __always_inline unsigned long read_ti_thread_flags(struct thread_info *ti)
{
	return READ_ONCE(ti->flags);
}

#define set_thread_flag(flag) \
	set_ti_thread_flag(current_thread_info(), flag)
#define clear_thread_flag(flag) \
	clear_ti_thread_flag(current_thread_info(), flag)
#define test_thread_flag(flag) \
	test_ti_thread_flag(current_thread_info(), flag)
#define read_thread_flags() \
	read_ti_thread_flags(current_thread_info())

#define clear_task_syscall_work(t, fl) \
	clear_bit(SYSCALL_WORK_BIT_##fl, &task_thread_info(t)->syscall_work)

#define tif_need_resched() test_thread_flag(TIF_NEED_RESCHED)

static inline void check_object_size(const void *ptr, unsigned long n,
				     bool to_user)
{ }

extern void __compiletime_error("copy source size is too small")
__bad_copy_from(void);
extern void __compiletime_error("copy destination size is too small")
__bad_copy_to(void);

static inline void copy_overflow(int size, unsigned long count)
{
}

static __always_inline __must_check bool
check_copy_size(const void *addr, size_t bytes, bool is_source)
{
	int sz = __builtin_object_size(addr, 0);
	if (unlikely(sz >= 0 && sz < bytes)) {
		if (!__builtin_constant_p(bytes))
			copy_overflow(sz, bytes);
		else if (is_source)
			__bad_copy_from();
		else
			__bad_copy_to();
		return false;
	}
	if (WARN_ON_ONCE(bytes > INT_MAX))
		return false;
	check_object_size(addr, bytes, is_source);
	return true;
}

#endif

#endif  
