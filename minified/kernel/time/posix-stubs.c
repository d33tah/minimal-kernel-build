
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
#define SYS_NI(name)  SYSCALL_ALIAS(sys_##name, sys_ni_posix_timers)
#endif

#ifndef COMPAT_SYS_NI
#define COMPAT_SYS_NI(name)  SYSCALL_ALIAS(compat_sys_##name, sys_ni_posix_timers)
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


SYS_NI(clock_settime);
SYS_NI(clock_gettime);
SYS_NI(clock_getres);
SYS_NI(clock_nanosleep);


#if defined(CONFIG_COMPAT) || defined(CONFIG_ALPHA)
COMPAT_SYS_NI(getitimer);
COMPAT_SYS_NI(setitimer);
#endif

