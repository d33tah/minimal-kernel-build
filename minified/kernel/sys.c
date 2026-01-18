/* Minimal syscall stubs - consolidated from multiple files */
#include <linux/syscalls.h>
#include <linux/utsname.h>
#include <linux/fs.h>

/* struct tms removed - never used */

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
/* uname/olduname replaced with COND_SYSCALL */
SYSCALL_DEFINE2(sethostname, char __user *, name, int, len)
{
	return -EPERM;
}
/* gethostname replaced with COND_SYSCALL */
SYSCALL_DEFINE2(setdomainname, char __user *, name, int, len)
{
	return -EPERM;
}
/* old_getrlimit replaced with COND_SYSCALL */
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
SYSCALL_DEFINE0(sync)
{
	return 0;
}
SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg)
{
	return -ENOTTY;
}
/* From mm/mlock.c - mprotect returns 0 not -ENOSYS */
SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len, unsigned long,
		prot)
{
	return 0;
}
/* From fs/fcntl.c */
SYSCALL_DEFINE3(fcntl, unsigned int, fd, unsigned int, cmd, unsigned long, arg)
{
	return -EINVAL;
}
SYSCALL_DEFINE3(fcntl64, unsigned int, fd, unsigned int, cmd, unsigned long,
		arg)
{
	return -EINVAL;
}
/* From fs/stat.c - readlink returns -EINVAL not -ENOSYS */
SYSCALL_DEFINE4(readlinkat, int, dfd, const char __user *, pathname,
		char __user *, buf, int, bufsiz)
{
	return -EINVAL;
}
SYSCALL_DEFINE3(readlink, const char __user *, path, char __user *, buf, int,
		bufsiz)
{
	return -EINVAL;
}
/* From fs/readdir.c */
SYSCALL_DEFINE3(getdents64, unsigned int, fd, struct linux_dirent64 __user *,
		dirent, unsigned int, count)
{
	return 0;
}
SYSCALL_DEFINE3(old_readdir, unsigned int, fd, struct old_linux_dirent __user *,
		dirent, unsigned int, count)
{
	return 0;
}
SYSCALL_DEFINE3(getdents, unsigned int, fd, struct linux_dirent __user *,
		dirent, unsigned int, count)
{
	return 0;
}
