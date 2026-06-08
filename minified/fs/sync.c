#include <linux/fs.h>
#include <linux/export.h>
#include <linux/syscalls.h>

int sync_filesystem(struct super_block *sb) { return 0; }

SYSCALL_DEFINE0(sync) { return 0; }
SYSCALL_DEFINE1(syncfs, int, fd) { return -ENOSYS; }

SYSCALL_DEFINE1(fsync, unsigned int, fd) { return -ENOSYS; }
SYSCALL_DEFINE1(fdatasync, unsigned int, fd) { return -ENOSYS; }

