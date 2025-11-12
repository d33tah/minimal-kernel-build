// SPDX-License-Identifier: GPL-2.0-or-later
/* Common capabilities, needed by capability.o - STUBBED */

#include <linux/capability.h>
#include <linux/audit.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/ptrace.h>
#include <linux/xattr.h>
#include <linux/hugetlb.h>
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/prctl.h>
#include <linux/securebits.h>
#include <linux/user_namespace.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/mnt_idmapping.h>

/* Stub implementations - all capability checks return success/allowed */

int cap_capable(const struct cred *cred, struct user_namespace *targ_ns,
		int cap, unsigned int opts)
{
	return 0; /* Always allowed */
}

int cap_settime(const struct timespec64 *ts, const struct timezone *tz)
{
	return 0;
}

int cap_ptrace_access_check(struct task_struct *child, unsigned int mode)
{
	return 0;
}

int cap_ptrace_traceme(struct task_struct *parent)
{
	return 0;
}

int cap_capget(struct task_struct *target, kernel_cap_t *effective,
	       kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	return 0;
}

int cap_capset(struct cred *new, const struct cred *old,
	       const kernel_cap_t *effective,
	       const kernel_cap_t *inheritable,
	       const kernel_cap_t *permitted)
{
	return 0;
}

int cap_inode_need_killpriv(struct dentry *dentry)
{
	return 0;
}

int cap_inode_killpriv(struct user_namespace *mnt_userns, struct dentry *dentry)
{
	return 0;
}

int cap_inode_getsecurity(struct user_namespace *mnt_userns,
			  struct inode *inode, const char *name, void **buffer,
			  bool alloc)
{
	return -EOPNOTSUPP;
}

int cap_convert_nscap(struct user_namespace *mnt_userns, struct dentry *dentry,
		      const void **ivalue, size_t size)
{
	return 0;
}

int get_vfs_caps_from_disk(struct user_namespace *mnt_userns,
			   const struct dentry *dentry,
			   struct cpu_vfs_cap_data *cpu_caps)
{
	return -ENODATA;
}

int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file)
{
	return 0;
}

int cap_inode_setxattr(struct dentry *dentry, const char *name,
		       const void *value, size_t size, int flags)
{
	return 0;
}

int cap_inode_removexattr(struct user_namespace *mnt_userns,
			  struct dentry *dentry, const char *name)
{
	return 0;
}

int cap_task_fix_setuid(struct cred *new, const struct cred *old, int flags)
{
	return 0;
}

int cap_task_setscheduler(struct task_struct *p)
{
	return 0;
}

int cap_task_setioprio(struct task_struct *p, int ioprio)
{
	return 0;
}

int cap_task_setnice(struct task_struct *p, int nice)
{
	return 0;
}

int cap_task_prctl(int option, unsigned long arg2, unsigned long arg3,
		   unsigned long arg4, unsigned long arg5)
{
	return -ENOSYS;
}

int cap_vm_enough_memory(struct mm_struct *mm, long pages)
{
	return 1; /* Always enough memory for cap check */
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
