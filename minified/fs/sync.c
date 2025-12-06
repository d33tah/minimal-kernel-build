#include <linux/fs.h>
#include <linux/export.h>
#include <linux/syscalls.h>

int sync_filesystem(struct super_block *sb) { return 0; }

void ksys_sync(void) { }
SYSCALL_DEFINE0(sync) { return 0; }
SYSCALL_DEFINE1(syncfs, int, fd) { return -ENOSYS; }

int vfs_fsync_range(struct file *file, loff_t start, loff_t end, int datasync) { return 0; }

int vfs_fsync(struct file *file, int datasync) { return 0; }

SYSCALL_DEFINE1(fsync, unsigned int, fd) { return -ENOSYS; }
SYSCALL_DEFINE1(fdatasync, unsigned int, fd) { return -ENOSYS; }

int sync_file_range(struct file *file, loff_t offset, loff_t nbytes, unsigned int flags) { return -ENOSYS; }
int ksys_sync_file_range(int fd, loff_t offset, loff_t nbytes, unsigned int flags) { return -ENOSYS; }
SYSCALL_DEFINE4(sync_file_range, int, fd, loff_t, offset, loff_t, nbytes, unsigned int, flags) { return -ENOSYS; }
SYSCALL_DEFINE4(sync_file_range2, int, fd, unsigned int, flags, loff_t, offset, loff_t, nbytes) { return -ENOSYS; }

void emergency_sync(void) { }
