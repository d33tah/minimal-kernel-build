 
 
#include <linux/fs.h>
#include <linux/pipe_fs_i.h>
#include <linux/export.h>
#include <linux/syscalls.h>

void pipe_lock(struct pipe_inode_info *pipe) { }

void pipe_unlock(struct pipe_inode_info *pipe) { }

bool generic_pipe_buf_try_steal(struct pipe_inode_info *pipe,
				struct pipe_buffer *buf) { return false; }

bool generic_pipe_buf_get(struct pipe_inode_info *pipe, struct pipe_buffer *buf) { return true; }

void generic_pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *buf) { }

long pipe_fcntl(struct file *file, unsigned int cmd, unsigned long arg) { return -EINVAL; }

SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags) { return -ENOSYS; }
SYSCALL_DEFINE1(pipe, int __user *, fildes) { return -ENOSYS; }

void free_pipe_info(struct pipe_inode_info *pipe) { }
const struct file_operations pipefifo_fops = { };

struct pipe_inode_info *get_pipe_info(struct file *file, bool for_splice) { return NULL; }
