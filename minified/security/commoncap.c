/* Minimal includes for capability stubs */
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/binfmts.h>
#include <linux/mm.h>


int cap_capable(const struct cred *cred, struct user_namespace *targ_ns,
		int cap, unsigned int opts)
{
	return 0;  
}

int cap_settime(const struct timespec64 *ts, const struct timezone *tz)
{
	return 0;
}

/* cap_ptrace_access_check, cap_ptrace_traceme, cap_capget, cap_capset removed - unused */

int cap_inode_need_killpriv(struct dentry *dentry)
{
	return 0;
}

int cap_inode_killpriv(struct user_namespace *mnt_userns, struct dentry *dentry)
{
	return 0;
}

/* cap_inode_getsecurity, cap_convert_nscap, get_vfs_caps_from_disk removed - unused */

int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file)
{
	return 0;
}

/* cap_inode_setxattr, cap_inode_removexattr, cap_task_* removed - unused */

int cap_vm_enough_memory(struct mm_struct *mm, long pages)
{
	return 1;  
}

int cap_mmap_addr(unsigned long addr)
{
	return 0;
}

int cap_mmap_file(struct file *file, unsigned long reqprot,
		  unsigned long prot, unsigned long flags)
{
	return 0;
}
