
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

#define CAP_OPT_NONE 0x0
#define CAP_OPT_NOAUDIT BIT(1)
#define CAP_OPT_INSETID BIT(2)

#define SECURITY_LSM_NATIVE_LABELS	1

struct ctl_table;
struct audit_krule;
struct user_namespace;
struct timezone;

enum lsm_event {
	LSM_POLICY_CHANGE,
};

/* Reduced lockdown enum for minimal kernel - only keep used values */
enum lockdown_reason {
	LOCKDOWN_NONE,
	LOCKDOWN_DEV_MEM,           /* Used by drivers/char/mem.c */
	LOCKDOWN_MODULE_PARAMETERS, /* Used by kernel/params.c */
	LOCKDOWN_CONFIDENTIALITY_MAX,
};

extern const char *const lockdown_reasons[LOCKDOWN_CONFIDENTIALITY_MAX+1];

extern int cap_capable(const struct cred *cred, struct user_namespace *ns,
		       int cap, unsigned int opts);
extern int cap_settime(const struct timespec64 *ts, const struct timezone *tz);
/* cap_ptrace_*, cap_capget, cap_capset removed - unused */
extern int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file);
/* cap_inode_setxattr, cap_inode_removexattr, cap_inode_getsecurity removed - unused */
int cap_inode_need_killpriv(struct dentry *dentry);
int cap_inode_killpriv(struct user_namespace *mnt_userns,
		       struct dentry *dentry);
extern int cap_mmap_addr(unsigned long addr);
extern int cap_mmap_file(struct file *file, unsigned long reqprot,
			 unsigned long prot, unsigned long flags);
/* cap_task_* externs removed - unused */
extern int cap_vm_enough_memory(struct mm_struct *mm, long pages);

struct msghdr;
struct sk_buff;
struct sock;
struct sockaddr;
struct socket;
struct dst_entry;
struct seq_file;
/* Removed unused: flowi_common, xfrm_*, sctp_association forward decls */

extern unsigned long mmap_min_addr;
extern unsigned long dac_mmap_min_addr;

/* LSM_SETID_* and LSM_PRLIMIT_* removed - unused in minimal kernel */

struct sched_param;
struct request_sock;

#define LSM_UNSAFE_SHARE	1
#define LSM_UNSAFE_PTRACE	2
#define LSM_UNSAFE_NO_NEW_PRIVS	4

extern int mmap_min_addr_handler(struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos);

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

static inline int security_capable(const struct cred *cred,
				   struct user_namespace *ns,
				   int cap,
				   unsigned int opts)
{
	return cap_capable(cred, ns, cap, opts);
}

/* security_settime64 removed - unused */

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

/* security_fs_context_dup removed - unused */
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

/* security_sb_remount removed - unused */

static inline int security_sb_kern_mount(struct super_block *sb)
{
	return 0;
}

/* security_sb_statfs removed - unused */

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

/* security_inode_init_security_anon removed - unused */

static inline int security_inode_create(struct inode *dir,
					 struct dentry *dentry,
					 umode_t mode)
{
	return 0;
}

/* security_inode_link, security_inode_unlink, security_inode_symlink removed - unused */

static inline int security_inode_mkdir(struct inode *dir,
					struct dentry *dentry,
					int mode)
{
	return 0;
}

/* security_inode_rmdir removed - unused */

static inline int security_inode_mknod(struct inode *dir,
					struct dentry *dentry,
					int mode, dev_t dev)
{
	return 0;
}

/* security_inode_rename, security_inode_readlink removed - unused */

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

/* security_file_permission removed - unused */

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

/* security_file_mprotect, security_file_receive removed - unused */

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

/* security_kernel_act_as and security_kernel_create_files_as removed - unused */

/* security_task_setnice, security_task_setscheduler, security_task_getscheduler removed - unused */

static inline int security_task_kill(struct task_struct *p,
				     struct kernel_siginfo *info, int sig,
				     const struct cred *cred)
{
	return 0;
}

static inline void security_d_instantiate(struct dentry *dentry,
					  struct inode *inode)
{ }

/* security_secctx_to_secid removed - unused */

static inline int security_locked_down(enum lockdown_reason what)
{
	return 0;
}

/* security_path_unlink, security_path_rmdir, security_path_chmod, security_path_chown removed - unused */

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
