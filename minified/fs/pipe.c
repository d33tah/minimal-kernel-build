// SPDX-License-Identifier: GPL-2.0
/* Stubbed pipe.c */
#include <linux/fs.h>
#include <linux/pipe_fs_i.h>
#include <linux/export.h>
#include <linux/syscalls.h>

void pipe_lock(struct pipe_inode_info *pipe) { }
EXPORT_SYMBOL(pipe_lock);

void pipe_unlock(struct pipe_inode_info *pipe) { }
EXPORT_SYMBOL(pipe_unlock);

bool generic_pipe_buf_try_steal(struct pipe_inode_info *pipe,
				struct pipe_buffer *buf) { return false; }
EXPORT_SYMBOL(generic_pipe_buf_try_steal);

bool generic_pipe_buf_get(struct pipe_inode_info *pipe, struct pipe_buffer *buf) { return true; }
EXPORT_SYMBOL(generic_pipe_buf_get);

void generic_pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *buf) { }
EXPORT_SYMBOL(generic_pipe_buf_release);

long pipe_fcntl(struct file *file, unsigned int cmd, unsigned long arg) { return -EINVAL; }

SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags) { return -ENOSYS; }
SYSCALL_DEFINE1(pipe, int __user *, fildes) { return -ENOSYS; }

void free_pipe_info(struct pipe_inode_info *pipe) { }
const struct file_operations pipefifo_fops = { };
