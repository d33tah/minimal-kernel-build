/* Minimal io_uring.h - stubs for !CONFIG_IO_URING */
#ifndef _LINUX_IO_URING_H
#define _LINUX_IO_URING_H

struct task_struct;
struct file;
struct sock;
struct io_uring_cmd;

static inline struct sock *io_uring_get_socket(struct file *file)
{
	return NULL;
}
static inline void io_uring_task_cancel(void)
{
}
static inline void io_uring_files_cancel(void)
{
}
static inline void io_uring_free(struct task_struct *tsk)
{
}
static inline const char *io_uring_get_opcode(u8 opcode)
{
	return "";
}

#endif
