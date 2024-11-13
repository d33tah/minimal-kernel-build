/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Linux Security Module Hook declarations.
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 * Copyright (C) 2015 Intel Corporation.
 * Copyright (C) 2015 Casey Schaufler <casey@schaufler-ca.com>
 * Copyright (C) 2016 Mellanox Techonologies
 * Copyright (C) 2020 Google LLC.
 */

/*
 * The macro LSM_HOOK is used to define the data structures required by
 * the LSM framework using the pattern:
 *
 *	LSM_HOOK(<return_type>, <default_value>, <hook_name>, args...)
 *
 * struct security_hook_heads {
 *   #define LSM_HOOK(RET, DEFAULT, NAME, ...) struct hlist_head NAME;
 *   #include <linux/lsm_hook_defs.h>
 *   #undef LSM_HOOK
 * };
 */
LSM_HOOK(int, 0, binder_set_context_mgr, const struct cred *mgr)
LSM_HOOK(int, 0, binder_transaction, const struct cred *from,
	 const struct cred *to)
LSM_HOOK(int, 0, binder_transfer_binder, const struct cred *from,
	 const struct cred *to)
LSM_HOOK(int, 0, binder_transfer_file, const struct cred *from,
	 const struct cred *to, struct file *file)
LSM_HOOK(int, 0, ptrace_access_check, struct task_struct *child,
	 unsigned int mode)
LSM_HOOK(int, 0, ptrace_traceme, struct task_struct *parent)
LSM_HOOK(int, 0, capget, struct task_struct *target, kernel_cap_t *effective,
	 kernel_cap_t *inheritable, kernel_cap_t *permitted)
LSM_HOOK(int, 0, capset, struct cred *new, const struct cred *old,
	 const kernel_cap_t *effective, const kernel_cap_t *inheritable,
	 const kernel_cap_t *permitted)
LSM_HOOK(int, 0, capable, const struct cred *cred, struct user_namespace *ns,
	 int cap, unsigned int opts)
LSM_HOOK(int, 0, quotactl, int cmds, int type, int id, struct super_block *sb)
LSM_HOOK(int, 0, quota_on, struct dentry *dentry)
LSM_HOOK(int, 0, syslog, int type)
LSM_HOOK(int, 0, settime, const struct timespec64 *ts,
	 const struct timezone *tz)
LSM_HOOK(int, 0, vm_enough_memory, struct mm_struct *mm, long pages)
LSM_HOOK(int, 0, bprm_creds_for_exec, struct linux_binprm *bprm)
LSM_HOOK(int, 0, bprm_creds_from_file, struct linux_binprm *bprm, struct file *file)
LSM_HOOK(int, 0, bprm_check_security, struct linux_binprm *bprm)
LSM_HOOK(void, LSM_RET_VOID, bprm_committing_creds, struct linux_binprm *bprm)
LSM_HOOK(void, LSM_RET_VOID, bprm_committed_creds, struct linux_binprm *bprm)
LSM_HOOK(int, 0, fs_context_dup, struct fs_context *fc,
	 struct fs_context *src_sc)
LSM_HOOK(int, -ENOPARAM, fs_context_parse_param, struct fs_context *fc,
	 struct fs_parameter *param)
LSM_HOOK(int, 0, sb_alloc_security, struct super_block *sb)
LSM_HOOK(void, LSM_RET_VOID, sb_delete, struct super_block *sb)
LSM_HOOK(void, LSM_RET_VOID, sb_free_security, struct super_block *sb)
LSM_HOOK(void, LSM_RET_VOID, sb_free_mnt_opts, void *mnt_opts)
LSM_HOOK(int, 0, sb_eat_lsm_opts, char *orig, void **mnt_opts)
LSM_HOOK(int, 0, sb_mnt_opts_compat, struct super_block *sb, void *mnt_opts)
LSM_HOOK(int, 0, sb_remount, struct super_block *sb, void *mnt_opts)
LSM_HOOK(int, 0, sb_kern_mount, struct super_block *sb)
LSM_HOOK(int, 0, sb_show_options, struct seq_file *m, struct super_block *sb)
LSM_HOOK(int, 0, sb_statfs, struct dentry *dentry)
LSM_HOOK(int, 0, sb_mount, const char *dev_name, const struct path *path,
	 const char *type, unsigned long flags, void *data)
LSM_HOOK(int, 0, sb_umount, struct vfsmount *mnt, int flags)
LSM_HOOK(int, 0, sb_pivotroot, const struct path *old_path,
	 const struct path *new_path)
LSM_HOOK(int, 0, sb_set_mnt_opts, struct super_block *sb, void *mnt_opts,
	 unsigned long kern_flags, unsigned long *set_kern_flags)
LSM_HOOK(int, 0, sb_clone_mnt_opts, const struct super_block *oldsb,
	 struct super_block *newsb, unsigned long kern_flags,
	 unsigned long *set_kern_flags)
LSM_HOOK(int, 0, move_mount, const struct path *from_path,
	 const struct path *to_path)
LSM_HOOK(int, -EOPNOTSUPP, dentry_init_security, struct dentry *dentry,
	 int mode, const struct qstr *name, const char **xattr_name,
	 void **ctx, u32 *ctxlen)
LSM_HOOK(int, 0, dentry_create_files_as, struct dentry *dentry, int mode,
	 struct qstr *name, const struct cred *old, struct cred *new)


/* Needed for inode based security check */
LSM_HOOK(int, 0, path_notify, const struct path *path, u64 mask,
	 unsigned int obj_type)
LSM_HOOK(int, 0, inode_alloc_security, struct inode *inode)
LSM_HOOK(void, LSM_RET_VOID, inode_free_security, struct inode *inode)
LSM_HOOK(int, 0, inode_init_security, struct inode *inode,
	 struct inode *dir, const struct qstr *qstr, const char **name,
	 void **value, size_t *len)
LSM_HOOK(int, 0, inode_init_security_anon, struct inode *inode,
	 const struct qstr *name, const struct inode *context_inode)
LSM_HOOK(int, 0, inode_create, struct inode *dir, struct dentry *dentry,
	 umode_t mode)
LSM_HOOK(int, 0, inode_link, struct dentry *old_dentry, struct inode *dir,
	 struct dentry *new_dentry)
LSM_HOOK(int, 0, inode_unlink, struct inode *dir, struct dentry *dentry)
LSM_HOOK(int, 0, inode_symlink, struct inode *dir, struct dentry *dentry,
	 const char *old_name)
LSM_HOOK(int, 0, inode_mkdir, struct inode *dir, struct dentry *dentry,
	 umode_t mode)
LSM_HOOK(int, 0, inode_rmdir, struct inode *dir, struct dentry *dentry)
LSM_HOOK(int, 0, inode_mknod, struct inode *dir, struct dentry *dentry,
	 umode_t mode, dev_t dev)
LSM_HOOK(int, 0, inode_rename, struct inode *old_dir, struct dentry *old_dentry,
	 struct inode *new_dir, struct dentry *new_dentry)
LSM_HOOK(int, 0, inode_readlink, struct dentry *dentry)
LSM_HOOK(int, 0, inode_follow_link, struct dentry *dentry, struct inode *inode,
	 bool rcu)
LSM_HOOK(int, 0, inode_permission, struct inode *inode, int mask)
LSM_HOOK(int, 0, inode_setattr, struct dentry *dentry, struct iattr *attr)
LSM_HOOK(int, 0, inode_getattr, const struct path *path)
LSM_HOOK(int, 0, inode_setxattr, struct user_namespace *mnt_userns,
	 struct dentry *dentry, const char *name, const void *value,
	 size_t size, int flags)
LSM_HOOK(void, LSM_RET_VOID, inode_post_setxattr, struct dentry *dentry,
	 const char *name, const void *value, size_t size, int flags)
LSM_HOOK(int, 0, inode_getxattr, struct dentry *dentry, const char *name)
LSM_HOOK(int, 0, inode_listxattr, struct dentry *dentry)
LSM_HOOK(int, 0, inode_removexattr, struct user_namespace *mnt_userns,
	 struct dentry *dentry, const char *name)
LSM_HOOK(int, 0, inode_need_killpriv, struct dentry *dentry)
LSM_HOOK(int, 0, inode_killpriv, struct user_namespace *mnt_userns,
	 struct dentry *dentry)
LSM_HOOK(int, -EOPNOTSUPP, inode_getsecurity, struct user_namespace *mnt_userns,
	 struct inode *inode, const char *name, void **buffer, bool alloc)
LSM_HOOK(int, -EOPNOTSUPP, inode_setsecurity, struct inode *inode,
	 const char *name, const void *value, size_t size, int flags)
LSM_HOOK(int, 0, inode_listsecurity, struct inode *inode, char *buffer,
	 size_t buffer_size)
LSM_HOOK(void, LSM_RET_VOID, inode_getsecid, struct inode *inode, u32 *secid)
LSM_HOOK(int, 0, inode_copy_up, struct dentry *src, struct cred **new)
LSM_HOOK(int, -EOPNOTSUPP, inode_copy_up_xattr, const char *name)
LSM_HOOK(int, 0, kernfs_init_security, struct kernfs_node *kn_dir,
	 struct kernfs_node *kn)
LSM_HOOK(int, 0, file_permission, struct file *file, int mask)
LSM_HOOK(int, 0, file_alloc_security, struct file *file)
LSM_HOOK(void, LSM_RET_VOID, file_free_security, struct file *file)
LSM_HOOK(int, 0, file_ioctl, struct file *file, unsigned int cmd,
	 unsigned long arg)
LSM_HOOK(int, 0, mmap_addr, unsigned long addr)
LSM_HOOK(int, 0, mmap_file, struct file *file, unsigned long reqprot,
	 unsigned long prot, unsigned long flags)
LSM_HOOK(int, 0, file_mprotect, struct vm_area_struct *vma,
	 unsigned long reqprot, unsigned long prot)
LSM_HOOK(int, 0, file_lock, struct file *file, unsigned int cmd)
LSM_HOOK(int, 0, file_fcntl, struct file *file, unsigned int cmd,
	 unsigned long arg)
LSM_HOOK(void, LSM_RET_VOID, file_set_fowner, struct file *file)
LSM_HOOK(int, 0, file_send_sigiotask, struct task_struct *tsk,
	 struct fown_struct *fown, int sig)
LSM_HOOK(int, 0, file_receive, struct file *file)
LSM_HOOK(int, 0, file_open, struct file *file)
LSM_HOOK(int, 0, task_alloc, struct task_struct *task,
	 unsigned long clone_flags)
LSM_HOOK(void, LSM_RET_VOID, task_free, struct task_struct *task)
LSM_HOOK(int, 0, cred_alloc_blank, struct cred *cred, gfp_t gfp)
LSM_HOOK(void, LSM_RET_VOID, cred_free, struct cred *cred)
LSM_HOOK(int, 0, cred_prepare, struct cred *new, const struct cred *old,
	 gfp_t gfp)
LSM_HOOK(void, LSM_RET_VOID, cred_transfer, struct cred *new,
	 const struct cred *old)
LSM_HOOK(void, LSM_RET_VOID, cred_getsecid, const struct cred *c, u32 *secid)
LSM_HOOK(int, 0, kernel_act_as, struct cred *new, u32 secid)
LSM_HOOK(int, 0, kernel_create_files_as, struct cred *new, struct inode *inode)
LSM_HOOK(int, 0, kernel_module_request, char *kmod_name)
LSM_HOOK(int, 0, kernel_load_data, enum kernel_load_data_id id, bool contents)
LSM_HOOK(int, 0, kernel_post_load_data, char *buf, loff_t size,
	 enum kernel_load_data_id id, char *description)
LSM_HOOK(int, 0, kernel_read_file, struct file *file,
	 enum kernel_read_file_id id, bool contents)
LSM_HOOK(int, 0, kernel_post_read_file, struct file *file, char *buf,
	 loff_t size, enum kernel_read_file_id id)
LSM_HOOK(int, 0, task_fix_setuid, struct cred *new, const struct cred *old,
	 int flags)
LSM_HOOK(int, 0, task_fix_setgid, struct cred *new, const struct cred * old,
	 int flags)
LSM_HOOK(int, 0, task_setpgid, struct task_struct *p, pid_t pgid)
LSM_HOOK(int, 0, task_getpgid, struct task_struct *p)
LSM_HOOK(int, 0, task_getsid, struct task_struct *p)
LSM_HOOK(void, LSM_RET_VOID, current_getsecid_subj, u32 *secid)
LSM_HOOK(void, LSM_RET_VOID, task_getsecid_obj,
	 struct task_struct *p, u32 *secid)
LSM_HOOK(int, 0, task_setnice, struct task_struct *p, int nice)
LSM_HOOK(int, 0, task_setioprio, struct task_struct *p, int ioprio)
LSM_HOOK(int, 0, task_getioprio, struct task_struct *p)
LSM_HOOK(int, 0, task_prlimit, const struct cred *cred,
	 const struct cred *tcred, unsigned int flags)
LSM_HOOK(int, 0, task_setrlimit, struct task_struct *p, unsigned int resource,
	 struct rlimit *new_rlim)
LSM_HOOK(int, 0, task_setscheduler, struct task_struct *p)
LSM_HOOK(int, 0, task_getscheduler, struct task_struct *p)
LSM_HOOK(int, 0, task_movememory, struct task_struct *p)
LSM_HOOK(int, 0, task_kill, struct task_struct *p, struct kernel_siginfo *info,
	 int sig, const struct cred *cred)
LSM_HOOK(int, -ENOSYS, task_prctl, int option, unsigned long arg2,
	 unsigned long arg3, unsigned long arg4, unsigned long arg5)
LSM_HOOK(void, LSM_RET_VOID, task_to_inode, struct task_struct *p,
	 struct inode *inode)
LSM_HOOK(int, 0, ipc_permission, struct kern_ipc_perm *ipcp, short flag)
LSM_HOOK(void, LSM_RET_VOID, ipc_getsecid, struct kern_ipc_perm *ipcp,
	 u32 *secid)
LSM_HOOK(int, 0, msg_msg_alloc_security, struct msg_msg *msg)
LSM_HOOK(void, LSM_RET_VOID, msg_msg_free_security, struct msg_msg *msg)
LSM_HOOK(int, 0, msg_queue_alloc_security, struct kern_ipc_perm *perm)
LSM_HOOK(void, LSM_RET_VOID, msg_queue_free_security,
	 struct kern_ipc_perm *perm)
LSM_HOOK(int, 0, msg_queue_associate, struct kern_ipc_perm *perm, int msqflg)
LSM_HOOK(int, 0, msg_queue_msgctl, struct kern_ipc_perm *perm, int cmd)
LSM_HOOK(int, 0, msg_queue_msgsnd, struct kern_ipc_perm *perm,
	 struct msg_msg *msg, int msqflg)
LSM_HOOK(int, 0, msg_queue_msgrcv, struct kern_ipc_perm *perm,
	 struct msg_msg *msg, struct task_struct *target, long type, int mode)
LSM_HOOK(int, 0, shm_alloc_security, struct kern_ipc_perm *perm)
LSM_HOOK(void, LSM_RET_VOID, shm_free_security, struct kern_ipc_perm *perm)
LSM_HOOK(int, 0, shm_associate, struct kern_ipc_perm *perm, int shmflg)
LSM_HOOK(int, 0, shm_shmctl, struct kern_ipc_perm *perm, int cmd)
LSM_HOOK(int, 0, shm_shmat, struct kern_ipc_perm *perm, char __user *shmaddr,
	 int shmflg)
LSM_HOOK(int, 0, sem_alloc_security, struct kern_ipc_perm *perm)
LSM_HOOK(void, LSM_RET_VOID, sem_free_security, struct kern_ipc_perm *perm)
LSM_HOOK(int, 0, sem_associate, struct kern_ipc_perm *perm, int semflg)
LSM_HOOK(int, 0, sem_semctl, struct kern_ipc_perm *perm, int cmd)
LSM_HOOK(int, 0, sem_semop, struct kern_ipc_perm *perm, struct sembuf *sops,
	 unsigned nsops, int alter)
LSM_HOOK(int, 0, netlink_send, struct sock *sk, struct sk_buff *skb)
LSM_HOOK(void, LSM_RET_VOID, d_instantiate, struct dentry *dentry,
	 struct inode *inode)
LSM_HOOK(int, -EINVAL, getprocattr, struct task_struct *p, char *name,
	 char **value)
LSM_HOOK(int, -EINVAL, setprocattr, const char *name, void *value, size_t size)
LSM_HOOK(int, 0, ismaclabel, const char *name)
LSM_HOOK(int, -EOPNOTSUPP, secid_to_secctx, u32 secid, char **secdata,
	 u32 *seclen)
LSM_HOOK(int, 0, secctx_to_secid, const char *secdata, u32 seclen, u32 *secid)
LSM_HOOK(void, LSM_RET_VOID, release_secctx, char *secdata, u32 seclen)
LSM_HOOK(void, LSM_RET_VOID, inode_invalidate_secctx, struct inode *inode)
LSM_HOOK(int, 0, inode_notifysecctx, struct inode *inode, void *ctx, u32 ctxlen)
LSM_HOOK(int, 0, inode_setsecctx, struct dentry *dentry, void *ctx, u32 ctxlen)
LSM_HOOK(int, 0, inode_getsecctx, struct inode *inode, void **ctx,
	 u32 *ctxlen)






/* key management security hooks */



LSM_HOOK(int, 0, locked_down, enum lockdown_reason what)

LSM_HOOK(int, 0, perf_event_open, struct perf_event_attr *attr, int type)
LSM_HOOK(int, 0, perf_event_alloc, struct perf_event *event)
LSM_HOOK(void, LSM_RET_VOID, perf_event_free, struct perf_event *event)
LSM_HOOK(int, 0, perf_event_read, struct perf_event *event)
LSM_HOOK(int, 0, perf_event_write, struct perf_event *event)

