#ifndef _LINUX_INIT_SYSCALLS_H
#define _LINUX_INIT_SYSCALLS_H
int __init init_mount(const char *dev_name, const char *dir_name, const char *type_page, unsigned long flags, void *data_page);
/* init_umount removed - never called */
int __init init_chdir(const char *filename);
int __init init_chroot(const char *filename);
int __init init_chown(const char *filename, uid_t user, gid_t group, int flags);
/* init_chmod removed - never called */
int __init init_eaccess(const char *filename);
/* init_stat removed - never called */
int __init init_mknod(const char *filename, umode_t mode, unsigned int dev);
int __init init_link(const char *oldname, const char *newname);
int __init init_symlink(const char *oldname, const char *newname);
/* init_unlink removed - never called */
/* init_mkdir removed - never called */
/* init_rmdir removed - never called */
int __init init_dup(struct file *file);
#endif
