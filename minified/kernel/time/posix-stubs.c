
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

/*
 * Only clock_gettime / clock_getres remain wired up: the x86 vDSO fallback
 * (asm/vdso/gettimeofday.h) references __NR_clock_gettime[64] and
 * __NR_clock_getres[_time64], so their syscall table entries must stay even
 * though the syscalls themselves are unreachable (-ENOSYS). All other posix
 * timer/itimer syscalls are removed from syscall_32.tbl (-> sys_ni).
 */
SYS_NI(clock_gettime);
SYS_NI(clock_getres);
