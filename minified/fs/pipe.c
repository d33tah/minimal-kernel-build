#include <linux/fs.h>
#include <linux/pipe_fs_i.h>
#include <linux/export.h>
#include <linux/syscalls.h>

/* pipe_lock, pipe_unlock, pipe_fcntl, generic_pipe_buf_release removed - unused */
/* generic_pipe_buf_try_steal, generic_pipe_buf_get removed - unused */

SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags) { return -ENOSYS; }
SYSCALL_DEFINE1(pipe, int __user *, fildes) { return -ENOSYS; }

void free_pipe_info(struct pipe_inode_info *pipe) { }
const struct file_operations pipefifo_fops = { };
