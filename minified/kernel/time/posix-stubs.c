
#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/posix-timers.h>
#include <linux/time_namespace.h>
#include <linux/compat.h>

#include <asm/syscall_wrapper.h>

asmlinkage long sys_ni_posix_timers(void)
{
	return -ENOSYS;
}

#ifndef SYS_NI
#define SYS_NI(name) SYSCALL_ALIAS(sys_##name, sys_ni_posix_timers)
#endif

#ifndef COMPAT_SYS_NI
#define COMPAT_SYS_NI(name) \
	SYSCALL_ALIAS(compat_sys_##name, sys_ni_posix_timers)
#endif

SYS_NI(timer_create);
SYS_NI(timer_gettime);
SYS_NI(timer_getoverrun);
SYS_NI(timer_settime);
SYS_NI(timer_delete);
SYS_NI(clock_adjtime);
SYS_NI(getitimer);
SYS_NI(setitimer);
SYS_NI(clock_adjtime32);
#ifdef __ARCH_WANT_SYS_ALARM
SYS_NI(alarm);
#endif

/* Stub: clock syscalls not needed for Hello World */
SYSCALL_DEFINE2(clock_settime, const clockid_t, which_clock,
		const struct __kernel_timespec __user *, tp)
{
	return -ENOSYS;
}

int do_clock_gettime(clockid_t which_clock, struct timespec64 *tp)
{
	tp->tv_sec = 0;
	tp->tv_nsec = 0;
	return 0;
}

SYSCALL_DEFINE2(clock_gettime, const clockid_t, which_clock,
		struct __kernel_timespec __user *, tp)
{
	return 0;
}

SYSCALL_DEFINE2(clock_getres, const clockid_t, which_clock,
		struct __kernel_timespec __user *, tp)
{
	return 0;
}

SYSCALL_DEFINE4(clock_nanosleep, const clockid_t, which_clock, int, flags,
		const struct __kernel_timespec __user *, rqtp,
		struct __kernel_timespec __user *, rmtp)
{
	return 0;
}

/* CONFIG_COMPAT not defined */
