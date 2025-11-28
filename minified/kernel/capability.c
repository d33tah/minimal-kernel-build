 
 

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/uaccess.h>

 

const kernel_cap_t __cap_empty_set = CAP_EMPTY_SET;

int file_caps_enabled = 1;

/* Stub: no_file_caps cmdline option not needed for minimal kernel */
static int __init file_caps_disable(char *str) { return 1; }
__setup("no_file_caps", file_caps_disable);


 
bool file_ns_capable(const struct file *file, struct user_namespace *ns,
		     int cap)
{

	if (WARN_ON_ONCE(!cap_valid(cap)))
		return false;

	if (security_capable(file->f_cred, ns, cap, CAP_OPT_NONE) == 0)
		return true;

	return false;
}

 
bool privileged_wrt_inode_uidgid(struct user_namespace *ns,
				 struct user_namespace *mnt_userns,
				 const struct inode *inode)
{
	return kuid_has_mapping(ns, i_uid_into_mnt(mnt_userns, inode)) &&
	       kgid_has_mapping(ns, i_gid_into_mnt(mnt_userns, inode));
}

 
bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
			      const struct inode *inode, int cap)
{
	struct user_namespace *ns = current_user_ns();

	return ns_capable(ns, cap) &&
	       privileged_wrt_inode_uidgid(ns, mnt_userns, inode);
}

 
bool ptracer_capable(struct task_struct *tsk, struct user_namespace *ns)
{
	int ret = 0;   
	const struct cred *cred;

	rcu_read_lock();
	cred = rcu_dereference(tsk->ptracer_cred);
	if (cred)
		ret = security_capable(cred, ns, CAP_SYS_PTRACE,
				       CAP_OPT_NOAUDIT);
	rcu_read_unlock();
	return (ret == 0);
}
