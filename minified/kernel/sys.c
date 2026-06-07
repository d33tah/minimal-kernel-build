/* Minimal includes for syscall stubs */
#include <linux/syscalls.h>
#include <linux/utsname.h>
#include <linux/resource.h>
#include <linux/sched.h>

#include <linux/rcupdate.h>
#include <linux/cred.h>
#include <linux/uaccess.h>
#include <linux/fs_struct.h>

int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;



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

SYSCALL_DEFINE1(umask, int, mask)
{
	mask = xchg(&current->fs->umask, mask & S_IRWXUGO);
	return mask;
}

