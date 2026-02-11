#ifndef _LINUX_INIT_SYSCALLS_H
#define _LINUX_INIT_SYSCALLS_H
int __init init_mount(const char *dev_name, const char *dir_name, const char *type_page, unsigned long flags, void *data_page);
/* init_umount, init_chdir, init_chown, init_chmod, init_stat,
   init_mknod, init_link, init_symlink, init_unlink, init_mkdir, init_rmdir removed */
int __init init_chroot(const char *filename);
int __init init_eaccess(const char *filename);
int __init init_dup(struct file *file);
#endif
