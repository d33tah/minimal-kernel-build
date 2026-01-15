#include <linux/fs.h>
#include <linux/pipe_fs_i.h>
/* pipe/pipe2 replaced with COND_SYSCALL */
/* free_pipe_info removed - empty stub, call removed from exit.c */
const struct file_operations pipefifo_fops = {};
