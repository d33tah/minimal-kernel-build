#include <linux/fs.h>
#include <linux/pipe_fs_i.h>
#include <linux/export.h>
#include <linux/syscalls.h>
SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags)
{
	return -ENOSYS;
}
SYSCALL_DEFINE1(pipe, int __user *, fildes)
{
	return -ENOSYS;
}
/* free_pipe_info removed - empty stub, call removed from exit.c */
const struct file_operations pipefifo_fops = {};
