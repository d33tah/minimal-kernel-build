// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/kernel/sys.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/utsname.h>
#include <linux/mman.h>
#include <linux/reboot.h>
#include <linux/prctl.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/kmod.h>
#include <linux/perf_event.h>
#include <linux/resource.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/key.h>
#include <linux/times.h>
#include <linux/posix-timers.h>
#include <linux/security.h>
#include <linux/suspend.h>
#include <linux/tty.h>
#include <linux/signal.h>
#include <linux/cn_proc.h>

#include <linux/task_io_accounting_ops.h>
#include <linux/seccomp.h>
#include <linux/cpu.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/fs_struct.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/gfp.h>
#include <linux/syscore_ops.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/syscall_user_dispatch.h>

#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/kprobes.h>
#include <linux/user_namespace.h>
#include <linux/time_namespace.h>
#include <linux/binfmts.h>

#include <linux/sched.h>
#include <linux/sched/autogroup.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/stat.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/task.h>
#include <linux/sched/cputime.h>
#include <linux/rcupdate.h>
#include <linux/uidgid.h>
#include <linux/cred.h>

#include <linux/nospec.h>

#include <linux/kmsg_dump.h>
/* Move somewhere else to avoid recompiling? */
#include <generated/utsrelease.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/unistd.h>

#include "uid16.h"

#ifndef SET_UNALIGN_CTL
# define SET_UNALIGN_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_UNALIGN_CTL
# define GET_UNALIGN_CTL(a, b)	(-EINVAL)
#endif
#ifndef SET_FPEMU_CTL
# define SET_FPEMU_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_FPEMU_CTL
# define GET_FPEMU_CTL(a, b)	(-EINVAL)
#endif
#ifndef SET_FPEXC_CTL
# define SET_FPEXC_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_FPEXC_CTL
# define GET_FPEXC_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_ENDIAN
# define GET_ENDIAN(a, b)	(-EINVAL)
#endif
#ifndef SET_ENDIAN
# define SET_ENDIAN(a, b)	(-EINVAL)
#endif
#ifndef GET_TSC_CTL
# define GET_TSC_CTL(a)		(-EINVAL)
#endif
#ifndef SET_TSC_CTL
# define SET_TSC_CTL(a)		(-EINVAL)
#endif
#ifndef GET_FP_MODE
# define GET_FP_MODE(a)		(-EINVAL)
#endif
#ifndef SET_FP_MODE
# define SET_FP_MODE(a,b)	(-EINVAL)
#endif
#ifndef SVE_SET_VL
# define SVE_SET_VL(a)		(-EINVAL)
#endif
#ifndef SVE_GET_VL
# define SVE_GET_VL()		(-EINVAL)
#endif
#ifndef SME_SET_VL
# define SME_SET_VL(a)		(-EINVAL)
#endif
#ifndef SME_GET_VL
# define SME_GET_VL()		(-EINVAL)
#endif
#ifndef PAC_RESET_KEYS
# define PAC_RESET_KEYS(a, b)	(-EINVAL)
#endif
#ifndef PAC_SET_ENABLED_KEYS
# define PAC_SET_ENABLED_KEYS(a, b, c)	(-EINVAL)
#endif
#ifndef PAC_GET_ENABLED_KEYS
# define PAC_GET_ENABLED_KEYS(a)	(-EINVAL)
#endif
#ifndef SET_TAGGED_ADDR_CTRL
# define SET_TAGGED_ADDR_CTRL(a)	(-EINVAL)
#endif
#ifndef GET_TAGGED_ADDR_CTRL
# define GET_TAGGED_ADDR_CTRL()		(-EINVAL)
#endif

/*
 * this is where the system-wide overflow UID and GID are defined, for
 * architectures that now have 32-bit UID/GID but didn't in the past
 */

int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;

EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);

/*
 * the same as above, but for filesystems which can only store a 16-bit
 * UID and GID. as such, this is needed on all architectures
 */

int fs_overflowuid = DEFAULT_FS_OVERFLOWUID;
int fs_overflowgid = DEFAULT_FS_OVERFLOWGID;

EXPORT_SYMBOL(fs_overflowuid);
EXPORT_SYMBOL(fs_overflowgid);

/* Stubbed priority syscalls - not needed for minimal kernel */
SYSCALL_DEFINE3(setpriority, int, which, int, who, int, niceval)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(getpriority, int, which, int, who)
{
	return 20; /* Return default priority */
}

/*
 * Unprivileged users may change the real gid to the effective gid
 * or vice versa.  (BSD-style)
 *
 * If you set the real gid at all, or set the effective gid to a value not
 * equal to the real gid, then the saved gid is set to the new effective gid.
 *
 * This makes it possible for a setgid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setregid() will be
 * 100% compatible with BSD.  A program which uses just setgid() will be
 * 100% compatible with POSIX with saved IDs.
 *
 * SMP: There are not races, the GIDs are checked only by filesystem
 *      operations (as far as semantic preservation is concerned).
 */

/**
 * sys_getpid - return the thread group id of the current process
 *
 * Note, despite the name, this returns the tgid not the pid.  The tgid and
 * the pid are identical unless CLONE_THREAD was specified on clone() in
 * which case the tgid is the same in all threads of the same group.
 *
 * This is SMP safe as current->tgid does not change.
 */
SYSCALL_DEFINE0(getpid)
{
	return task_tgid_vnr(current);
}

/* Thread ID - the internal kernel "pid" */
SYSCALL_DEFINE0(gettid)
{
	return task_pid_vnr(current);
}

/*
 * Accessing ->real_parent is not SMP-safe, it could
 * change from under us. However, we can use a stale
 * value of ->real_parent under rcu_read_lock(), see
 * release_task()->call_rcu(delayed_put_task_struct).
 */
SYSCALL_DEFINE0(getppid)
{
	int pid;

	rcu_read_lock();
	pid = task_tgid_vnr(rcu_dereference(current->real_parent));
	rcu_read_unlock();

	return pid;
}

SYSCALL_DEFINE0(getuid)
{
	/* Only we change this so SMP safe */
	return from_kuid_munged(current_user_ns(), current_uid());
}

SYSCALL_DEFINE0(geteuid)
{
	/* Only we change this so SMP safe */
	return from_kuid_munged(current_user_ns(), current_euid());
}

SYSCALL_DEFINE0(getgid)
{
	/* Only we change this so SMP safe */
	return from_kgid_munged(current_user_ns(), current_gid());
}

SYSCALL_DEFINE0(getegid)
{
	/* Only we change this so SMP safe */
	return from_kgid_munged(current_user_ns(), current_egid());
}

/* Stubbed times syscall - not needed for minimal kernel */
SYSCALL_DEFINE1(times, struct tms __user *, tbuf)
{
	/* Return all zeros for CPU times */
	if (tbuf) {
		struct tms tmp = {0};
		if (copy_to_user(tbuf, &tmp, sizeof(struct tms)))
			return -EFAULT;
	}
	return 0;
}


/*
 * This needs some heavy checking ...
 * I just haven't the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 *
 * OK, I think I have the protection semantics right.... this is really
 * only important on a multi-user system anyway, to make sure one user
 * can't send a signal to a process owned by another.  -TYT, 12/12/91
 *
 * !PF_FORKNOEXEC check to conform completely to POSIX.
 */
/* Stubbed process group and session management - not needed for minimal kernel */
SYSCALL_DEFINE2(setpgid, pid_t, pid, pid_t, pgid)
{
	return 0; /* Pretend success */
}

SYSCALL_DEFINE1(getpgid, pid_t, pid)
{
	return 1; /* Return a dummy process group ID */
}

#ifdef __ARCH_WANT_SYS_GETPGRP
SYSCALL_DEFINE0(getpgrp)
{
	return 1; /* Return a dummy process group ID */
}
#endif

SYSCALL_DEFINE1(getsid, pid_t, pid)
{
	return 1; /* Return a dummy session ID */
}

int ksys_setsid(void)
{
	return 1; /* Return a dummy session ID */
}

SYSCALL_DEFINE0(setsid)
{
	return ksys_setsid();
}

DECLARE_RWSEM(uts_sem);

#ifdef COMPAT_UTS_MACHINE
#define override_architecture(name) \
	(personality(current->personality) == PER_LINUX32 && \
	 copy_to_user(name->machine, COMPAT_UTS_MACHINE, \
		      sizeof(COMPAT_UTS_MACHINE)))
#else
#define override_architecture(name)	0
#endif

/*
 * Work around broken programs that cannot handle "Linux 3.0".
 * Instead we map 3.x to 2.6.40+x, so e.g. 3.0 would be 2.6.40
 * And we map 4.x and later versions to 2.6.60+x, so 4.0/5.0/6.0/... would be
 * 2.6.60.
 */
static int override_release(char __user *release, size_t len)
{
	int ret = 0;

	if (current->personality & UNAME26) {
		const char *rest = UTS_RELEASE;
		char buf[65] = { 0 };
		int ndots = 0;
		unsigned v;
		size_t copy;

		while (*rest) {
			if (*rest == '.' && ++ndots >= 3)
				break;
			if (!isdigit(*rest) && *rest != '.')
				break;
			rest++;
		}
		v = LINUX_VERSION_PATCHLEVEL + 60;
		copy = clamp_t(size_t, len, 1, sizeof(buf));
		copy = scnprintf(buf, copy, "2.6.%u%s", v, rest);
		ret = copy_to_user(release, buf, copy + 1);
	}
	return ret;
}

SYSCALL_DEFINE1(newuname, struct new_utsname __user *, name)
{
	struct new_utsname tmp;

	down_read(&uts_sem);
	memcpy(&tmp, utsname(), sizeof(tmp));
	up_read(&uts_sem);
	if (copy_to_user(name, &tmp, sizeof(tmp)))
		return -EFAULT;

	if (override_release(name->release, sizeof(name->release)))
		return -EFAULT;
	if (override_architecture(name))
		return -EFAULT;
	return 0;
}

#ifdef __ARCH_WANT_SYS_OLD_UNAME
/*
 * Old cruft
 */
SYSCALL_DEFINE1(uname, struct old_utsname __user *, name)
{
	struct old_utsname tmp;

	if (!name)
		return -EFAULT;

	down_read(&uts_sem);
	memcpy(&tmp, utsname(), sizeof(tmp));
	up_read(&uts_sem);
	if (copy_to_user(name, &tmp, sizeof(tmp)))
		return -EFAULT;

	if (override_release(name->release, sizeof(name->release)))
		return -EFAULT;
	if (override_architecture(name))
		return -EFAULT;
	return 0;
}

SYSCALL_DEFINE1(olduname, struct oldold_utsname __user *, name)
{
	struct oldold_utsname tmp;

	if (!name)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	down_read(&uts_sem);
	memcpy(&tmp.sysname, &utsname()->sysname, __OLD_UTS_LEN);
	memcpy(&tmp.nodename, &utsname()->nodename, __OLD_UTS_LEN);
	memcpy(&tmp.release, &utsname()->release, __OLD_UTS_LEN);
	memcpy(&tmp.version, &utsname()->version, __OLD_UTS_LEN);
	memcpy(&tmp.machine, &utsname()->machine, __OLD_UTS_LEN);
	up_read(&uts_sem);
	if (copy_to_user(name, &tmp, sizeof(tmp)))
		return -EFAULT;

	if (override_architecture(name))
		return -EFAULT;
	if (override_release(name->release, sizeof(name->release)))
		return -EFAULT;
	return 0;
}
#endif

/* Stubbed sethostname - not needed for minimal kernel */
SYSCALL_DEFINE2(sethostname, char __user *, name, int, len)
{
	return -EPERM;
}

#ifdef __ARCH_WANT_SYS_GETHOSTNAME

SYSCALL_DEFINE2(gethostname, char __user *, name, int, len)
{
	int i;
	struct new_utsname *u;
	char tmp[__NEW_UTS_LEN + 1];

	if (len < 0)
		return -EINVAL;
	down_read(&uts_sem);
	u = utsname();
	i = 1 + strlen(u->nodename);
	if (i > len)
		i = len;
	memcpy(tmp, u->nodename, i);
	up_read(&uts_sem);
	if (copy_to_user(name, tmp, i))
		return -EFAULT;
	return 0;
}

#endif

/* Stubbed setdomainname - not needed for minimal kernel */
SYSCALL_DEFINE2(setdomainname, char __user *, name, int, len)
{
	return -EPERM;
}

/* make sure you are allowed to change @tsk limits before calling this */
/* Stubbed: Resource limits not needed for minimal "Hello World" system */
SYSCALL_DEFINE2(getrlimit, unsigned int, resource, struct rlimit __user *, rlim)
{
	struct rlimit value = { .rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY };
	return copy_to_user(rlim, &value, sizeof(value)) ? -EFAULT : 0;
}

#ifdef __ARCH_WANT_SYS_OLD_GETRLIMIT
SYSCALL_DEFINE2(old_getrlimit, unsigned int, resource, struct rlimit __user *, rlim)
{
	struct rlimit x = { .rlim_cur = 0x7FFFFFFF, .rlim_max = 0x7FFFFFFF };
	return copy_to_user(rlim, &x, sizeof(x)) ? -EFAULT : 0;
}
#endif

/* Stubbed: prlimit64 and setrlimit - not needed for minimal system */
SYSCALL_DEFINE4(prlimit64, pid_t, pid, unsigned int, resource,
		const struct rlimit64 __user *, new_rlim,
		struct rlimit64 __user *, old_rlim)
{
	struct rlimit64 old64 = { .rlim_cur = RLIM64_INFINITY, .rlim_max = RLIM64_INFINITY };
	if (old_rlim && copy_to_user(old_rlim, &old64, sizeof(old64)))
		return -EFAULT;
	return 0;
}

SYSCALL_DEFINE2(setrlimit, unsigned int, resource, struct rlimit __user *, rlim)
{
	return 0; /* Silently ignore - not needed for "Hello World" */
}

/* Stubbed: rusage simplified for minimal system */
void getrusage(struct task_struct *p, int who, struct rusage *r)
{
	memset(r, 0, sizeof(*r));
}

SYSCALL_DEFINE2(getrusage, int, who, struct rusage __user *, ru)
{
	struct rusage r;
	getrusage(current, who, &r);
	return copy_to_user(ru, &r, sizeof(r)) ? -EFAULT : 0;
}


SYSCALL_DEFINE1(umask, int, mask)
{
	mask = xchg(&current->fs->umask, mask & S_IRWXUGO);
	return mask;
}

int __weak arch_prctl_spec_ctrl_get(struct task_struct *t, unsigned long which)
{
	return -EINVAL;
}

int __weak arch_prctl_spec_ctrl_set(struct task_struct *t, unsigned long which,
				    unsigned long ctrl)
{
	return -EINVAL;
}

SYSCALL_DEFINE5(prctl, int, option, unsigned long, arg2, unsigned long, arg3,
		unsigned long, arg4, unsigned long, arg5)
{
	return -EINVAL;
}

SYSCALL_DEFINE3(getcpu, unsigned __user *, cpup, unsigned __user *, nodep,
		struct getcpu_cache __user *, unused)
{
	int err = 0;
	int cpu = raw_smp_processor_id();

	if (cpup)
		err |= put_user(cpu, cpup);
	if (nodep)
		err |= put_user(cpu_to_node(cpu), nodep);
	return err ? -EFAULT : 0;
}

/**
 * do_sysinfo - fill in sysinfo struct
 * @info: pointer to buffer to fill
 */
/* Stubbed sysinfo syscall - not needed for minimal kernel */
SYSCALL_DEFINE1(sysinfo, struct sysinfo __user *, info)
{
	struct sysinfo val = {0};

	/* Return minimal stub data */
	val.uptime = 1;
	val.procs = 1;
	val.mem_unit = 1;

	if (copy_to_user(info, &val, sizeof(struct sysinfo)))
		return -EFAULT;

	return 0;
}

