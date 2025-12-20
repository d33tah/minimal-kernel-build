/* Minimal security.h - stubs for !CONFIG_SECURITY */
#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/kernel_read_file.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mm.h>

struct linux_binprm;
struct cred;
struct super_block;
struct inode;
struct dentry;
struct file;
struct path;
struct xattr;
struct mm_struct;
struct user_namespace;
struct fs_context;
struct fs_parameter;

/* Reduced lockdown enum for minimal kernel - only keep used values */
enum lockdown_reason {
	LOCKDOWN_NONE,
	LOCKDOWN_DEV_MEM,           /* Used by drivers/char/mem.c */
	LOCKDOWN_MODULE_PARAMETERS, /* Used by kernel/params.c */
	LOCKDOWN_CONFIDENTIALITY_MAX,
};

extern int cap_capable(const struct cred *cred, struct user_namespace *ns,
		       int cap, unsigned int opts);
/* cap_settime, cap_ptrace_*, cap_capget, cap_capset, cap_inode_*,
   cap_mmap_addr, cap_vm_enough_memory removed - unused */
extern int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file);

extern unsigned long mmap_min_addr;

#define LSM_UNSAFE_SHARE	1
#define LSM_UNSAFE_PTRACE	2
#define LSM_UNSAFE_NO_NEW_PRIVS	4


typedef int (*initxattrs) (struct inode *inode,
			   const struct xattr *xattr_array, void *fs_data);


#define __data_id_enumify(ENUM, dummy) LOADING_ ## ENUM,
#define __data_id_stringify(dummy, str) #str,

enum kernel_load_data_id {
	__kernel_read_file_id(__data_id_enumify)
};


static inline void security_free_mnt_opts(void **mnt_opts)
{
}


static inline int security_init(void)
{
	return 0;
}

static inline int early_security_init(void)
{
	return 0;
}



static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return __vm_enough_memory(mm, pages, 1);  /* Stub: always assume capability present */
}

static inline int security_bprm_creds_for_exec(struct linux_binprm *bprm)
{
	return 0;
}

static inline int security_bprm_creds_from_file(struct linux_binprm *bprm,
						struct file *file)
{
	return cap_bprm_creds_from_file(bprm, file);
}

static inline int security_bprm_check(struct linux_binprm *bprm)
{
	return 0;
}

static inline void security_bprm_committing_creds(struct linux_binprm *bprm)
{
}

static inline void security_bprm_committed_creds(struct linux_binprm *bprm)
{
}

static inline int security_fs_context_parse_param(struct fs_context *fc,
						  struct fs_parameter *param)
{
	return -ENOPARAM;
}

static inline int security_sb_alloc(struct super_block *sb)
{
	return 0;
}

static inline void security_sb_delete(struct super_block *sb)
{ }

static inline void security_sb_free(struct super_block *sb)
{ }

static inline int security_sb_eat_lsm_opts(char *options,
					   void **mnt_opts)
{
	return 0;
}


static inline int security_sb_kern_mount(struct super_block *sb)
{
	return 0;
}


static inline int security_sb_mount(const char *dev_name, const struct path *path,
				    const char *type, unsigned long flags,
				    void *data)
{
	return 0;
}

static inline int security_sb_umount(struct vfsmount *mnt, int flags)
{
	return 0;
}

static inline int security_sb_set_mnt_opts(struct super_block *sb,
					   void *mnt_opts,
					   unsigned long kern_flags,
					   unsigned long *set_kern_flags)
{
	return 0;
}

static inline int security_inode_alloc(struct inode *inode)
{
	return 0;
}

static inline void security_inode_free(struct inode *inode)
{ }




static inline int security_inode_mkdir(struct inode *dir,
					struct dentry *dentry,
					int mode)
{
	return 0;
}


static inline int security_inode_mknod(struct inode *dir,
					struct dentry *dentry,
					int mode, dev_t dev)
{
	return 0;
}


static inline int security_inode_follow_link(struct dentry *dentry,
					     struct inode *inode,
					     bool rcu)
{
	return 0;
}

static inline int security_inode_permission(struct inode *inode, int mask)
{
	return 0;
}

static inline int security_inode_setattr(struct dentry *dentry,
					  struct iattr *attr)
{
	return 0;
}

static inline int security_inode_need_killpriv(struct dentry *dentry)
{
	return 0;  /* Stub: capability checking disabled */
}

static inline int security_inode_killpriv(struct user_namespace *mnt_userns,
					  struct dentry *dentry)
{
	return 0;  /* Stub: capability checking disabled */
}


static inline int security_file_alloc(struct file *file)
{
	return 0;
}

static inline void security_file_free(struct file *file)
{ }

static inline int security_mmap_file(struct file *file, unsigned long prot,
				     unsigned long flags)
{
	return 0;
}

static inline int security_mmap_addr(unsigned long addr)
{
	return 0;  /* Stub: capability checking disabled */
}


static inline int security_file_open(struct file *file)
{
	return 0;
}

static inline int security_task_alloc(struct task_struct *task,
				      unsigned long clone_flags)
{
	return 0;
}

static inline void security_task_free(struct task_struct *task)
{ }


static inline void security_cred_free(struct cred *cred)
{ }

static inline int security_prepare_creds(struct cred *new,
					 const struct cred *old,
					 gfp_t gfp)
{
	return 0;
}



static inline int security_task_kill(struct task_struct *p,
				     struct kernel_siginfo *info, int sig,
				     const struct cred *cred)
{
	return 0;
}

static inline void security_d_instantiate(struct dentry *dentry,
					  struct inode *inode)
{ }


static inline int security_locked_down(enum lockdown_reason what)
{
	return 0;
}


static inline int security_path_truncate(const struct path *path)
{
	return 0;
}

static inline int security_path_symlink(const struct path *dir, struct dentry *dentry,
					const char *old_name)
{
	return 0;
}

static inline int security_path_link(struct dentry *old_dentry,
				     const struct path *new_dir,
				     struct dentry *new_dentry)
{
	return 0;
}

static inline int security_path_mkdir(const struct path *dir, struct dentry *dentry,
				      umode_t mode)
{
	return 0;
}

static inline int security_path_mknod(const struct path *dir, struct dentry *dentry,
				      umode_t mode, unsigned int dev)
{
	return 0;
}

static inline int security_path_chroot(const struct path *path)
{
	return 0;
}





/* Removed perf_event_attr, perf_event forward decls - unnecessary in security.h */

#endif  
