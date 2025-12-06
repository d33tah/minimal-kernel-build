/* Minimal includes for syscall stubs */
#include <linux/syscalls.h>
#include <linux/utsname.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/cred.h>
#include <linux/uaccess.h>
#include <linux/fs_struct.h>

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


int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;



int fs_overflowuid = DEFAULT_FS_OVERFLOWUID;
int fs_overflowgid = DEFAULT_FS_OVERFLOWGID;


SYSCALL_DEFINE3(setpriority, int, which, int, who, int, niceval)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(getpriority, int, which, int, who)
{
	return 20;  
}


SYSCALL_DEFINE0(getpid)
{
	return task_tgid_vnr(current);
}

SYSCALL_DEFINE0(gettid)
{
	return task_pid_vnr(current);
}

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
	 
	return from_kuid_munged(current_user_ns(), current_uid());
}

SYSCALL_DEFINE0(geteuid)
{
	 
	return from_kuid_munged(current_user_ns(), current_euid());
}

SYSCALL_DEFINE0(getgid)
{
	 
	return from_kgid_munged(current_user_ns(), current_gid());
}

SYSCALL_DEFINE0(getegid)
{
	 
	return from_kgid_munged(current_user_ns(), current_egid());
}

SYSCALL_DEFINE1(times, struct tms __user *, tbuf)
{
	 
	if (tbuf) {
		struct tms tmp = {0};
		if (copy_to_user(tbuf, &tmp, sizeof(struct tms)))
			return -EFAULT;
	}
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

int ksys_setsid(void)
{
	return 1;  
}

SYSCALL_DEFINE0(setsid)
{
	return ksys_setsid();
}

DECLARE_RWSEM(uts_sem);

SYSCALL_DEFINE1(newuname, struct new_utsname __user *, name)
{
	struct new_utsname tmp;

	down_read(&uts_sem);
	memcpy(&tmp, utsname(), sizeof(tmp));
	up_read(&uts_sem);
	if (copy_to_user(name, &tmp, sizeof(tmp)))
		return -EFAULT;
	return 0;
}

#ifdef __ARCH_WANT_SYS_OLD_UNAME
/* Stub: old uname syscalls not needed for minimal kernel */
SYSCALL_DEFINE1(uname, struct old_utsname __user *, name) { return -ENOSYS; }
SYSCALL_DEFINE1(olduname, struct oldold_utsname __user *, name) { return -ENOSYS; }
#endif

SYSCALL_DEFINE2(sethostname, char __user *, name, int, len)
{
	return -EPERM;
}

#ifdef __ARCH_WANT_SYS_GETHOSTNAME
/* Stub: gethostname syscall not needed for minimal kernel */
SYSCALL_DEFINE2(gethostname, char __user *, name, int, len) { return -ENOSYS; }
#endif

SYSCALL_DEFINE2(setdomainname, char __user *, name, int, len)
{
	return -EPERM;
}

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
	return 0;  
}

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

SYSCALL_DEFINE1(sysinfo, struct sysinfo __user *, info)
{
	struct sysinfo val = {0};

	 
	val.uptime = 1;
	val.procs = 1;
	val.mem_unit = 1;

	if (copy_to_user(info, &val, sizeof(struct sysinfo)))
		return -EFAULT;

	return 0;
}

