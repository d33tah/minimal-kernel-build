#ifndef _LINUX_PIPE_FS_I_H
#define _LINUX_PIPE_FS_I_H

#define PIPE_DEF_BUFFERS	16

#define PIPE_BUF_FLAG_LRU	0x01	 
#define PIPE_BUF_FLAG_ATOMIC	0x02	 
#define PIPE_BUF_FLAG_GIFT	0x04	 
#define PIPE_BUF_FLAG_PACKET	0x08	 
#define PIPE_BUF_FLAG_CAN_MERGE	0x10	 
#define PIPE_BUF_FLAG_WHOLE	0x20	 

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
	unsigned int r_counter;
	unsigned int w_counter;
	bool poll_usage;
	struct page *tmp_page;
	struct fasync_struct *fasync_readers;
	struct fasync_struct *fasync_writers;
	struct pipe_buffer *bufs;
	struct user_struct *user;
};

struct pipe_buf_operations {
	 
	int (*confirm)(struct pipe_inode_info *, struct pipe_buffer *);

	 
	void (*release)(struct pipe_inode_info *, struct pipe_buffer *);

	 
	bool (*try_steal)(struct pipe_inode_info *, struct pipe_buffer *);

	 
	bool (*get)(struct pipe_inode_info *, struct pipe_buffer *);
};

static inline bool pipe_empty(unsigned int head, unsigned int tail)
{
	return head == tail;
}

static inline unsigned int pipe_occupancy(unsigned int head, unsigned int tail)
{
	return head - tail;
}

static inline bool pipe_full(unsigned int head, unsigned int tail,
			     unsigned int limit)
{
	return pipe_occupancy(head, tail) >= limit;
}

static inline void pipe_buf_release(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	const struct pipe_buf_operations *ops = buf->ops;

	buf->ops = NULL;
	ops->release(pipe, buf);
}

#define PIPE_SIZE		PAGE_SIZE


void free_pipe_info(struct pipe_inode_info *);

#endif
