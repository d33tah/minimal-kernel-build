#ifndef _LINUX_PIPE_FS_I_H
#define _LINUX_PIPE_FS_I_H

struct pipe_buffer {
	struct page *page;
	unsigned int offset, len;
	const struct pipe_buf_operations *ops;
	unsigned int flags;
	unsigned long private;
};

struct pipe_inode_info {
	struct mutex mutex;
	wait_queue_head_t rd_wait, wr_wait;
	unsigned int head;
	unsigned int tail;
	unsigned int readers;
	unsigned int writers;
	unsigned int files;
	struct page *tmp_page;
	struct pipe_buffer *bufs;
	struct user_struct *user;
};

struct pipe_buf_operations;


#endif
