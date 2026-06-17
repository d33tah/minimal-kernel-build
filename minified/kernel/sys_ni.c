
#include <linux/linkage.h>
#include <linux/errno.h>

#include <asm/unistd.h>

#include <asm/syscall_wrapper.h>

asmlinkage long sys_ni_syscall(void);

asmlinkage long sys_ni_syscall(void)
{
	return -ENOSYS;
}

#ifndef COND_SYSCALL
#define COND_SYSCALL(name) cond_syscall(sys_##name)
#endif

#ifndef COND_SYSCALL_COMPAT
#define COND_SYSCALL_COMPAT(name) cond_syscall(compat_sys_##name)
#endif

/*
 * Only the cond_syscall() weak -ENOSYS fallbacks for syscalls that still
 * have an active slot in arch/x86/entry/syscalls/syscall_32.tbl are kept.
 * Entries for syscalls no longer in the table need no weak fallback.
 */

COND_SYSCALL(capget);
COND_SYSCALL(capset);

COND_SYSCALL(msync);
COND_SYSCALL(mincore);
COND_SYSCALL(madvise);
COND_SYSCALL(process_madvise);
COND_SYSCALL(process_mrelease);

COND_SYSCALL(time32);
COND_SYSCALL(stime32);
COND_SYSCALL(utime32);
COND_SYSCALL(adjtimex_time32);
COND_SYSCALL(nanosleep_time32);
COND_SYSCALL(clock_gettime32);
COND_SYSCALL(clock_getres_time32);
COND_SYSCALL(utimes_time32);
COND_SYSCALL(futimesat_time32);
COND_SYSCALL(pselect6_time32);
COND_SYSCALL_COMPAT(pselect6_time32);
COND_SYSCALL(ppoll_time32);
COND_SYSCALL_COMPAT(ppoll_time32);
COND_SYSCALL(utimensat_time32);

COND_SYSCALL(rseq);
