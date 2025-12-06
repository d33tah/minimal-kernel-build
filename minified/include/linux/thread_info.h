
#ifndef _LINUX_THREAD_INFO_H
#define _LINUX_THREAD_INFO_H

#include <linux/types.h>
#include <linux/limits.h>
#include <linux/bug.h>
#include <linux/time64.h>

/* Inlined from restart_block.h */
struct timespec;
struct old_timespec32;
struct pollfd;
enum timespec_type {
	TT_NONE		= 0,
	TT_NATIVE	= 1,
};
struct restart_block {
	unsigned long arch_data;
	long (*fn)(struct restart_block *);
	union {
		struct {
			u32 __user *uaddr;
			u32 val;
			u32 flags;
			u32 bitset;
			u64 time;
			u32 __user *uaddr2;
		} futex;
		struct {
			clockid_t clockid;
			enum timespec_type type;
			union {
				struct __kernel_timespec __user *rmtp;
				struct old_timespec32 __user *compat_rmtp;
			};
			u64 expires;
		} nanosleep;
		struct {
			struct pollfd __user *ufds;
			int nfds;
			int has_timeout;
			unsigned long tv_sec;
			unsigned long tv_nsec;
		} poll;
	};
};
extern long do_no_restart_syscall(struct restart_block *parm);
/* End of inlined restart_block.h content */

#include <linux/errno.h>

#include <asm/current.h>
#define current_thread_info() ((struct thread_info *)current)

#include <linux/bitops.h>

enum {
	BAD_STACK = -1,
	NOT_STACK = 0,
	GOOD_FRAME,
	GOOD_STACK,
};

enum syscall_work_bit {
	SYSCALL_WORK_BIT_SECCOMP,
	SYSCALL_WORK_BIT_SYSCALL_TRACEPOINT,
	SYSCALL_WORK_BIT_SYSCALL_TRACE,
	SYSCALL_WORK_BIT_SYSCALL_EMU,
	SYSCALL_WORK_BIT_SYSCALL_AUDIT,
	SYSCALL_WORK_BIT_SYSCALL_USER_DISPATCH,
	SYSCALL_WORK_BIT_SYSCALL_EXIT_TRAP,
};

#define SYSCALL_WORK_SECCOMP		BIT(SYSCALL_WORK_BIT_SECCOMP)
#define SYSCALL_WORK_SYSCALL_TRACEPOINT	BIT(SYSCALL_WORK_BIT_SYSCALL_TRACEPOINT)
#define SYSCALL_WORK_SYSCALL_TRACE	BIT(SYSCALL_WORK_BIT_SYSCALL_TRACE)
#define SYSCALL_WORK_SYSCALL_EMU	BIT(SYSCALL_WORK_BIT_SYSCALL_EMU)
#define SYSCALL_WORK_SYSCALL_AUDIT	BIT(SYSCALL_WORK_BIT_SYSCALL_AUDIT)
#define SYSCALL_WORK_SYSCALL_USER_DISPATCH BIT(SYSCALL_WORK_BIT_SYSCALL_USER_DISPATCH)
#define SYSCALL_WORK_SYSCALL_EXIT_TRAP	BIT(SYSCALL_WORK_BIT_SYSCALL_EXIT_TRAP)

#include <asm/thread_info.h>

#ifdef __KERNEL__

#ifndef arch_set_restart_data
#define arch_set_restart_data(restart) do { } while (0)
#endif

static inline long set_restart_fn(struct restart_block *restart,
					long (*fn)(struct restart_block *))
{
	restart->fn = fn;
	arch_set_restart_data(restart);
	return -ERESTART_RESTARTBLOCK;
}

#ifndef THREAD_ALIGN
#define THREAD_ALIGN	THREAD_SIZE
#endif

#define THREADINFO_GFP		(GFP_KERNEL_ACCOUNT | __GFP_ZERO)


static inline void set_ti_thread_flag(struct thread_info *ti, int flag)
{
	set_bit(flag, (unsigned long *)&ti->flags);
}

static inline void clear_ti_thread_flag(struct thread_info *ti, int flag)
{
	clear_bit(flag, (unsigned long *)&ti->flags);
}

static inline void update_ti_thread_flag(struct thread_info *ti, int flag,
					 bool value)
{
	if (value)
		set_ti_thread_flag(ti, flag);
	else
		clear_ti_thread_flag(ti, flag);
}

static inline int test_and_set_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_and_set_bit(flag, (unsigned long *)&ti->flags);
}

static inline int test_and_clear_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_and_clear_bit(flag, (unsigned long *)&ti->flags);
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
#define update_thread_flag(flag, value) \
	update_ti_thread_flag(current_thread_info(), flag, value)
#define test_and_set_thread_flag(flag) \
	test_and_set_ti_thread_flag(current_thread_info(), flag)
#define test_and_clear_thread_flag(flag) \
	test_and_clear_ti_thread_flag(current_thread_info(), flag)
#define test_thread_flag(flag) \
	test_ti_thread_flag(current_thread_info(), flag)
#define read_thread_flags() \
	read_ti_thread_flags(current_thread_info())

#define read_task_thread_flags(t) \
	read_ti_thread_flags(task_thread_info(t))

#define set_syscall_work(fl) \
	set_bit(SYSCALL_WORK_BIT_##fl, &current_thread_info()->syscall_work)
#define test_syscall_work(fl) \
	test_bit(SYSCALL_WORK_BIT_##fl, &current_thread_info()->syscall_work)
#define clear_syscall_work(fl) \
	clear_bit(SYSCALL_WORK_BIT_##fl, &current_thread_info()->syscall_work)

#define set_task_syscall_work(t, fl) \
	set_bit(SYSCALL_WORK_BIT_##fl, &task_thread_info(t)->syscall_work)
#define test_task_syscall_work(t, fl) \
	test_bit(SYSCALL_WORK_BIT_##fl, &task_thread_info(t)->syscall_work)
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

void __copy_overflow(int size, unsigned long count);

static inline void copy_overflow(int size, unsigned long count)
{
	if (IS_ENABLED(CONFIG_BUG))
		__copy_overflow(size, count);
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

#ifndef arch_setup_new_exec
static inline void arch_setup_new_exec(void) { }
#endif

#endif	 

#endif  
