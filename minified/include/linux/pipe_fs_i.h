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
	unsigned int max_usage;
	unsigned int ring_size;
	unsigned int nr_accounted;
	unsigned int readers;
	unsigned int writers;
	unsigned int files;
	/* r_counter, w_counter, poll_usage, tmp_page, fasync_readers, fasync_writers removed - never accessed */
	struct pipe_buffer *bufs;
	struct user_struct *user;
};

struct pipe_buf_operations {
	void (*release)(struct pipe_inode_info *, struct pipe_buffer *);
	/* confirm, try_steal, get callbacks removed - never invoked */
};

/* pipe_empty, pipe_occupancy, pipe_full, pipe_buf_release removed - never called */

#endif
