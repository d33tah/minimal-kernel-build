/*
 * Linux Security plug
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 * Copyright (C) 2016 Mellanox Techonologies
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Due to this file being licensed under the GPL there is controversy over
 *	whether this permits you to write a module that #includes this file
 *	without placing your module under the GPL.  Please consult a lawyer for
 *	advice before doing this.
 *
 */

#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/kernel_read_file.h>
#include <linux/key.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/mm.h>

struct linux_binprm;
struct cred;
struct rlimit;
struct kernel_siginfo;
struct sembuf;
struct kern_ipc_perm;
struct audit_context;
struct super_block;
struct inode;
struct dentry;
struct file;
struct vfsmount;
struct path;
struct qstr;
struct iattr;
struct fown_struct;
struct file_operations;
struct msg_msg;
struct xattr;
struct kernfs_node;
struct xfrm_sec_ctx;
struct mm_struct;
struct fs_context;
struct fs_parameter;
enum fs_value_type;
struct watch;
struct watch_notification;

/* Default (no) options for the capable function */
#define CAP_OPT_NONE 0x0
/* If capable should audit the security request */
#define CAP_OPT_NOAUDIT BIT(1)
/* If capable is being called by a setid function */
#define CAP_OPT_INSETID BIT(2)

/* LSM Agnostic defines for fs_context::lsm_flags */
#define SECURITY_LSM_NATIVE_LABELS	1

struct ctl_table;
struct audit_krule;
struct user_namespace;
struct timezone;

enum lsm_event {
	LSM_POLICY_CHANGE,
};

/*
 * These are reasons that can be passed to the security_locked_down()
 * LSM hook. Lockdown reasons that protect kernel integrity (ie, the
 * ability for userland to modify kernel code) are placed before
 * LOCKDOWN_INTEGRITY_MAX.  Lockdown reasons that protect kernel
 * confidentiality (ie, the ability for userland to extract
 * information from the running kernel that would otherwise be
 * restricted) are placed before LOCKDOWN_CONFIDENTIALITY_MAX.
 *
 * LSM authors should note that the semantics of any given lockdown
 * reason are not guaranteed to be stable - the same reason may block
 * one set of features in one kernel release, and a slightly different
 * set of features in a later kernel release. LSMs that seek to expose
 * lockdown policy at any level of granularity other than "none",
 * "integrity" or "confidentiality" are responsible for either
 * ensuring that they expose a consistent level of functionality to
 * userland, or ensuring that userland is aware that this is
 * potentially a moving target. It is easy to misuse this information
 * in a way that could break userspace. Please be careful not to do
 * so.
 *
 * If you add to this, remember to extend lockdown_reasons in
 * security/lockdown/lockdown.c.
 */
enum lockdown_reason {
	LOCKDOWN_NONE,
	LOCKDOWN_MODULE_SIGNATURE,
	LOCKDOWN_DEV_MEM,
	LOCKDOWN_EFI_TEST,
	LOCKDOWN_KEXEC,
	LOCKDOWN_HIBERNATION,
	LOCKDOWN_PCI_ACCESS,
	LOCKDOWN_IOPORT,
	LOCKDOWN_MSR,
	LOCKDOWN_ACPI_TABLES,
	LOCKDOWN_PCMCIA_CIS,
	LOCKDOWN_TIOCSSERIAL,
	LOCKDOWN_MODULE_PARAMETERS,
	LOCKDOWN_MMIOTRACE,
	LOCKDOWN_DEBUGFS,
	LOCKDOWN_XMON_WR,
	LOCKDOWN_BPF_WRITE_USER,
	LOCKDOWN_DBG_WRITE_KERNEL,
	LOCKDOWN_INTEGRITY_MAX,
	LOCKDOWN_KCORE,
	LOCKDOWN_KPROBES,
	LOCKDOWN_BPF_READ_KERNEL,
	LOCKDOWN_DBG_READ_KERNEL,
	LOCKDOWN_PERF,
	LOCKDOWN_TRACEFS,
	LOCKDOWN_XMON_RW,
	LOCKDOWN_XFRM_SECRET,
	LOCKDOWN_CONFIDENTIALITY_MAX,
};

extern const char *const lockdown_reasons[LOCKDOWN_CONFIDENTIALITY_MAX+1];

/* These functions are in security/commoncap.c */
extern int cap_capable(const struct cred *cred, struct user_namespace *ns,
		       int cap, unsigned int opts);
extern int cap_settime(const struct timespec64 *ts, const struct timezone *tz);
extern int cap_ptrace_access_check(struct task_struct *child, unsigned int mode);
extern int cap_ptrace_traceme(struct task_struct *parent);
extern int cap_capget(struct task_struct *target, kernel_cap_t *effective, kernel_cap_t *inheritable, kernel_cap_t *permitted);
extern int cap_capset(struct cred *new, const struct cred *old,
		      const kernel_cap_t *effective,
		      const kernel_cap_t *inheritable,
		      const kernel_cap_t *permitted);
extern int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file);
int cap_inode_setxattr(struct dentry *dentry, const char *name,
		       const void *value, size_t size, int flags);
int cap_inode_removexattr(struct user_namespace *mnt_userns,
			  struct dentry *dentry, const char *name);
int cap_inode_need_killpriv(struct dentry *dentry);
int cap_inode_killpriv(struct user_namespace *mnt_userns,
		       struct dentry *dentry);
int cap_inode_getsecurity(struct user_namespace *mnt_userns,
			  struct inode *inode, const char *name, void **buffer,
			  bool alloc);
extern int cap_mmap_addr(unsigned long addr);
extern int cap_mmap_file(struct file *file, unsigned long reqprot,
			 unsigned long prot, unsigned long flags);
extern int cap_task_fix_setuid(struct cred *new, const struct cred *old, int flags);
extern int cap_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			  unsigned long arg4, unsigned long arg5);
extern int cap_task_setscheduler(struct task_struct *p);
extern int cap_task_setioprio(struct task_struct *p, int ioprio);
extern int cap_task_setnice(struct task_struct *p, int nice);
extern int cap_vm_enough_memory(struct mm_struct *mm, long pages);

struct msghdr;
struct sk_buff;
struct sock;
struct sockaddr;
struct socket;
struct flowi_common;
struct dst_entry;
struct xfrm_selector;
struct xfrm_policy;
struct xfrm_state;
struct xfrm_user_sec_ctx;
struct seq_file;
struct sctp_association;

extern unsigned long mmap_min_addr;
extern unsigned long dac_mmap_min_addr;

/*
 * Values used in the task_security_ops calls
 */
/* setuid or setgid, id0 == uid or gid */
#define LSM_SETID_ID	1

/* setreuid or setregid, id0 == real, id1 == eff */
#define LSM_SETID_RE	2

/* setresuid or setresgid, id0 == real, id1 == eff, uid2 == saved */
#define LSM_SETID_RES	4

/* setfsuid or setfsgid, id0 == fsuid or fsgid */
#define LSM_SETID_FS	8

/* Flags for security_task_prlimit(). */
#define LSM_PRLIMIT_READ  1
#define LSM_PRLIMIT_WRITE 2

/* forward declares to avoid warnings */
struct sched_param;
struct request_sock;

/* bprm->unsafe reasons */
#define LSM_UNSAFE_SHARE	1
#define LSM_UNSAFE_PTRACE	2
#define LSM_UNSAFE_PTRACE	2
#define LSM_UNSAFE_NO_NEW_PRIVS	4

extern int mmap_min_addr_handler(struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos);

/* security_inode_init_security callback function to write xattrs */
typedef int (*initxattrs) (struct inode *inode,
			   const struct xattr *xattr_array, void *fs_data);


/* Keep the kernel_load_data_id enum in sync with kernel_read_file_id */
#define __data_id_enumify(ENUM, dummy) LOADING_ ## ENUM,
#define __data_id_stringify(dummy, str) #str,

enum kernel_load_data_id {
	__kernel_read_file_id(__data_id_enumify)
};

static const char * const kernel_load_data_str[] = {
	__kernel_read_file_id(__data_id_stringify)
};

static inline const char *kernel_load_data_id_str(enum kernel_load_data_id id)
{
	if ((unsigned)id >= LOADING_MAX_ID)
		return kernel_load_data_str[LOADING_UNKNOWN];

	return kernel_load_data_str[id];
}


static inline int call_blocking_lsm_notifier(enum lsm_event event, void *data)
{
	return 0;
}

static inline int register_blocking_lsm_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline  int unregister_blocking_lsm_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline void security_free_mnt_opts(void **mnt_opts)
{
}

/*
 * This is the default capabilities functionality.  Most of these functions
 * are just stubbed out, but a few must call the proper capable code.
 */

static inline int security_init(void)
{
	return 0;
}

static inline int early_security_init(void)
{
	return 0;
}

static inline int security_capable(const struct cred *cred,
				   struct user_namespace *ns,
				   int cap,
				   unsigned int opts)
{
	return cap_capable(cred, ns, cap, opts);
}

static inline int security_settime64(const struct timespec64 *ts,
				     const struct timezone *tz)
{
	return cap_settime(ts, tz);
}

static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return __vm_enough_memory(mm, pages, cap_vm_enough_memory(mm, pages));
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

static inline int security_fs_context_dup(struct fs_context *fc,
					  struct fs_context *src_fc)
{
	return 0;
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

static inline int security_sb_remount(struct super_block *sb,
				      void *mnt_opts)
{
	return 0;
}

static inline int security_sb_kern_mount(struct super_block *sb)
{
	return 0;
}

static inline int security_sb_statfs(struct dentry *dentry)
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

static inline int security_sb_pivotroot(const struct path *old_path,
					const struct path *new_path)
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

static inline int security_move_mount(const struct path *from_path,
				      const struct path *to_path)
{
	return 0;
}

static inline int security_inode_alloc(struct inode *inode)
{
	return 0;
}

static inline void security_inode_free(struct inode *inode)
{ }

static inline int security_inode_init_security_anon(struct inode *inode,
						    const struct qstr *name,
						    const struct inode *context_inode)
{
	return 0;
}

static inline int security_inode_create(struct inode *dir,
					 struct dentry *dentry,
					 umode_t mode)
{
	return 0;
}

static inline int security_inode_link(struct dentry *old_dentry,
				       struct inode *dir,
				       struct dentry *new_dentry)
{
	return 0;
}

static inline int security_inode_unlink(struct inode *dir,
					 struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_symlink(struct inode *dir,
					  struct dentry *dentry,
					  const char *old_name)
{
	return 0;
}

static inline int security_inode_mkdir(struct inode *dir,
					struct dentry *dentry,
					int mode)
{
	return 0;
}

static inline int security_inode_rmdir(struct inode *dir,
					struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_mknod(struct inode *dir,
					struct dentry *dentry,
					int mode, dev_t dev)
{
	return 0;
}

static inline int security_inode_rename(struct inode *old_dir,
					 struct dentry *old_dentry,
					 struct inode *new_dir,
					 struct dentry *new_dentry,
					 unsigned int flags)
{
	return 0;
}

static inline int security_inode_readlink(struct dentry *dentry)
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
	return cap_inode_need_killpriv(dentry);
}

static inline int security_inode_killpriv(struct user_namespace *mnt_userns,
					  struct dentry *dentry)
{
	return cap_inode_killpriv(mnt_userns, dentry);
}

static inline int security_file_permission(struct file *file, int mask)
{
	return 0;
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
	return cap_mmap_addr(addr);
}

static inline int security_file_mprotect(struct vm_area_struct *vma,
					 unsigned long reqprot,
					 unsigned long prot)
{
	return 0;
}

static inline int security_file_receive(struct file *file)
{
	return 0;
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

static inline int security_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	return 0;
}

static inline void security_cred_free(struct cred *cred)
{ }

static inline int security_prepare_creds(struct cred *new,
					 const struct cred *old,
					 gfp_t gfp)
{
	return 0;
}

static inline int security_kernel_act_as(struct cred *cred, u32 secid)
{
	return 0;
}

static inline int security_kernel_create_files_as(struct cred *cred,
						  struct inode *inode)
{
	return 0;
}

static inline int security_task_setnice(struct task_struct *p, int nice)
{
	return cap_task_setnice(p, nice);
}

static inline int security_task_setscheduler(struct task_struct *p)
{
	return cap_task_setscheduler(p);
}

static inline int security_task_getscheduler(struct task_struct *p)
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

static inline int security_secctx_to_secid(const char *secdata,
					   u32 seclen,
					   u32 *secid)
{
	return -EOPNOTSUPP;
}

static inline int security_locked_down(enum lockdown_reason what)
{
	return 0;
}

static inline int security_path_unlink(const struct path *dir, struct dentry *dentry)
{
	return 0;
}

static inline int security_path_mkdir(const struct path *dir, struct dentry *dentry,
				      umode_t mode)
{
	return 0;
}

static inline int security_path_rmdir(const struct path *dir, struct dentry *dentry)
{
	return 0;
}

static inline int security_path_mknod(const struct path *dir, struct dentry *dentry,
				      umode_t mode, unsigned int dev)
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

static inline int security_path_rename(const struct path *old_dir,
				       struct dentry *old_dentry,
				       const struct path *new_dir,
				       struct dentry *new_dentry,
				       unsigned int flags)
{
	return 0;
}

static inline int security_path_chmod(const struct path *path, umode_t mode)
{
	return 0;
}

static inline int security_path_chown(const struct path *path, kuid_t uid, kgid_t gid)
{
	return 0;
}

static inline int security_path_chroot(const struct path *path)
{
	return 0;
}




static inline struct dentry *securityfs_create_dir(const char *name,
						   struct dentry *parent)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *securityfs_create_file(const char *name,
						    umode_t mode,
						    struct dentry *parent,
						    void *data,
						    const struct file_operations *fops)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *securityfs_create_symlink(const char *name,
					struct dentry *parent,
					const char *target,
					const struct inode_operations *iops)
{
	return ERR_PTR(-ENODEV);
}

static inline void securityfs_remove(struct dentry *dentry)
{}



struct perf_event_attr;
struct perf_event;

#endif /* ! __LINUX_SECURITY_H */
