// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed open.c */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/fcntl.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

int do_truncate(struct user_namespace *mnt_userns, struct dentry *dentry,
		loff_t length, unsigned int time_attrs, struct file *filp) { return 0; }

long vfs_truncate(const struct path *path, loff_t length) { return 0; }
EXPORT_SYMBOL_GPL(vfs_truncate);

SYSCALL_DEFINE2(truncate, const char __user *, path, long, length) { return -ENOSYS; }
SYSCALL_DEFINE2(ftruncate, unsigned int, fd, unsigned long, length) { return -ENOSYS; }
SYSCALL_DEFINE2(truncate64, const char __user *, path, loff_t, length) { return -ENOSYS; }
SYSCALL_DEFINE2(ftruncate64, unsigned int, fd, loff_t, length) { return -ENOSYS; }

int vfs_fallocate(struct file *file, int mode, loff_t offset, loff_t len) { return 0; }
EXPORT_SYMBOL_GPL(vfs_fallocate);

SYSCALL_DEFINE4(fallocate, int, fd, int, mode, loff_t, offset, loff_t, len) { return -ENOSYS; }

int vfs_open(const struct path *path, struct file *file) { return 0; }

SYSCALL_DEFINE3(faccessat, int, dfd, const char __user *, filename, int, mode) { return -ENOSYS; }
SYSCALL_DEFINE4(faccessat2, int, dfd, const char __user *, filename, int, mode, int, flags) { return -ENOSYS; }
SYSCALL_DEFINE2(access, const char __user *, filename, int, mode) { return -ENOSYS; }

SYSCALL_DEFINE1(chdir, const char __user *, filename) { return -ENOSYS; }
SYSCALL_DEFINE1(fchdir, unsigned int, fd) { return -ENOSYS; }
SYSCALL_DEFINE1(chroot, const char __user *, filename) { return -ENOSYS; }

SYSCALL_DEFINE2(fchmod, unsigned int, fd, umode_t, mode) { return -ENOSYS; }
SYSCALL_DEFINE3(fchmodat, int, dfd, const char __user *, filename, umode_t, mode) { return -ENOSYS; }
SYSCALL_DEFINE2(chmod, const char __user *, filename, umode_t, mode) { return -ENOSYS; }

SYSCALL_DEFINE5(fchownat, int, dfd, const char __user *, filename, uid_t, user,
		 gid_t, group, int, flag) { return -ENOSYS; }
SYSCALL_DEFINE3(chown, const char __user *, filename, uid_t, user, gid_t, group) { return -ENOSYS; }
SYSCALL_DEFINE3(lchown, const char __user *, filename, uid_t, user, gid_t, group) { return -ENOSYS; }
SYSCALL_DEFINE3(fchown, unsigned int, fd, uid_t, user, gid_t, group) { return -ENOSYS; }

int finish_open(struct file *file, struct dentry *dentry,
		int (*open)(struct inode *, struct file *)) { return 0; }
EXPORT_SYMBOL(finish_open);

int finish_no_open(struct file *file, struct dentry *dentry) { return 1; }
EXPORT_SYMBOL(finish_no_open);

char *file_path(struct file *filp, char *buf, int buflen) { return buf; }
EXPORT_SYMBOL(file_path);

struct file *dentry_open(const struct path *path, int flags, const struct cred *cred) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(dentry_open);

struct file *dentry_create(const struct path *path, int flags, umode_t mode,
			   const struct cred *cred) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(dentry_create);

struct file *open_with_fake_path(const struct path *path, int flags,
				 struct inode *inode, const struct cred *cred) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(open_with_fake_path);

struct file *filp_open(const char *filename, int flags, umode_t mode) { return ERR_PTR(-ENOENT); }
EXPORT_SYMBOL(filp_open);

struct file *file_open_root(const struct path *root, const char *filename, int flags, umode_t mode) { return ERR_PTR(-ENOENT); }
EXPORT_SYMBOL(file_open_root);

long do_sys_open(int dfd, const char __user *filename, int flags, umode_t mode) { return -ENOSYS; }

SYSCALL_DEFINE3(open, const char __user *, filename, int, flags, umode_t, mode) { return -ENOSYS; }
SYSCALL_DEFINE4(openat, int, dfd, const char __user *, filename, int, flags, umode_t, mode) { return -ENOSYS; }
SYSCALL_DEFINE4(openat2, int, dfd, const char __user *, filename,
		 struct open_how __user *, how, size_t, usize) { return -ENOSYS; }

SYSCALL_DEFINE1(close, unsigned int, fd) { return -ENOSYS; }
SYSCALL_DEFINE2(creat, const char __user *, pathname, umode_t, mode) { return -ENOSYS; }
SYSCALL_DEFINE0(vhangup) { return -ENOSYS; }

int chown_common(const struct path *path, uid_t user, gid_t group) { return 0; }
int chmod_common(const struct path *path, umode_t mode) { return 0; }
long do_sys_truncate(const char __user *pathname, loff_t length) { return -ENOSYS; }
long do_sys_ftruncate(unsigned int fd, loff_t length, int small) { return -ENOSYS; }
int nonseekable_open(struct inode *inode, struct file *filp) { return 0; }
int ksys_fallocate(int fd, int mode, loff_t offset, loff_t len) { return -ENOSYS; }
