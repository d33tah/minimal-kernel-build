/* Minimal syscall stubs */
#include <linux/syscalls.h>
#include <linux/utsname.h>

struct tms {
	__kernel_clock_t tms_utime;
	__kernel_clock_t tms_stime;
	__kernel_clock_t tms_cutime;
	__kernel_clock_t tms_cstime;
};

int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;

/* All syscalls stubbed to return simple values */
SYSCALL_DEFINE2(getpriority, int, which, int, who)
{
	return 20;
}
SYSCALL_DEFINE0(getpid)
{
	return 1;
}
SYSCALL_DEFINE0(gettid)
{
	return 1;
}
SYSCALL_DEFINE0(getppid)
{
	return 0;
}
SYSCALL_DEFINE0(getuid)
{
	return 0;
}
SYSCALL_DEFINE0(geteuid)
{
	return 0;
}
SYSCALL_DEFINE0(getgid)
{
	return 0;
}
SYSCALL_DEFINE0(getegid)
{
	return 0;
}
SYSCALL_DEFINE2(setpgid, pid_t, pid, pid_t, pgid)
{
	return 0;
}
SYSCALL_DEFINE1(getpgid, pid_t, pid)
{
	return 1;
}
#ifdef __ARCH_WANT_SYS_GETPGRP
SYSCALL_DEFINE0(getpgrp)
{
	return 1;
}
#endif
SYSCALL_DEFINE1(getsid, pid_t, pid)
{
	return 1;
}
SYSCALL_DEFINE0(setsid)
{
	return 1;
}
#ifdef __ARCH_WANT_SYS_OLD_UNAME
SYSCALL_DEFINE1(uname, struct old_utsname __user *, name)
{
	return -ENOSYS;
}
SYSCALL_DEFINE1(olduname, struct oldold_utsname __user *, name)
{
	return -ENOSYS;
}
#endif
SYSCALL_DEFINE2(sethostname, char __user *, name, int, len)
{
	return -EPERM;
}
#ifdef __ARCH_WANT_SYS_GETHOSTNAME
SYSCALL_DEFINE2(gethostname, char __user *, name, int, len)
{
	return -ENOSYS;
}
#endif
SYSCALL_DEFINE2(setdomainname, char __user *, name, int, len)
{
	return -EPERM;
}
#ifdef __ARCH_WANT_SYS_OLD_GETRLIMIT
SYSCALL_DEFINE2(old_getrlimit, unsigned int, resource, struct rlimit __user *,
		rlim)
{
	return -ENOSYS;
}
#endif
SYSCALL_DEFINE2(setrlimit, unsigned int, resource, struct rlimit __user *, rlim)
{
	return 0;
}
SYSCALL_DEFINE1(umask, int, mask)
{
	return 0;
}
SYSCALL_DEFINE5(prctl, int, option, unsigned long, arg2, unsigned long, arg3,
		unsigned long, arg4, unsigned long, arg5)
{
	return -EINVAL;
}
SYSCALL_DEFINE1(personality, unsigned int, personality)
{
	return 0;
}
