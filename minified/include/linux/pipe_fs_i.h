 
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

 
static inline unsigned int pipe_space_for_user(unsigned int head, unsigned int tail,
					       struct pipe_inode_info *pipe)
{
	unsigned int p_occupancy, p_space;

	p_occupancy = pipe_occupancy(head, tail);
	if (p_occupancy >= pipe->max_usage)
		return 0;
	p_space = pipe->ring_size - p_occupancy;
	if (p_space > pipe->max_usage)
		p_space = pipe->max_usage;
	return p_space;
}

 
static inline __must_check bool pipe_buf_get(struct pipe_inode_info *pipe,
				struct pipe_buffer *buf)
{
	return buf->ops->get(pipe, buf);
}

 
static inline void pipe_buf_release(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	const struct pipe_buf_operations *ops = buf->ops;

	buf->ops = NULL;
	ops->release(pipe, buf);
}

 
static inline int pipe_buf_confirm(struct pipe_inode_info *pipe,
				   struct pipe_buffer *buf)
{
	if (!buf->ops->confirm)
		return 0;
	return buf->ops->confirm(pipe, buf);
}

 
static inline bool pipe_buf_try_steal(struct pipe_inode_info *pipe,
		struct pipe_buffer *buf)
{
	if (!buf->ops->try_steal)
		return false;
	return buf->ops->try_steal(pipe, buf);
}

 
#define PIPE_SIZE		PAGE_SIZE

 
void pipe_lock(struct pipe_inode_info *);
void pipe_unlock(struct pipe_inode_info *);
void pipe_double_lock(struct pipe_inode_info *, struct pipe_inode_info *);

 
void pipe_wait_readable(struct pipe_inode_info *);
void pipe_wait_writable(struct pipe_inode_info *);

struct pipe_inode_info *alloc_pipe_info(void);
void free_pipe_info(struct pipe_inode_info *);

 
bool generic_pipe_buf_get(struct pipe_inode_info *, struct pipe_buffer *);
bool generic_pipe_buf_try_steal(struct pipe_inode_info *, struct pipe_buffer *);
void generic_pipe_buf_release(struct pipe_inode_info *, struct pipe_buffer *);

extern const struct pipe_buf_operations nosteal_pipe_buf_ops;


 
long pipe_fcntl(struct file *, unsigned int, unsigned long arg);
struct pipe_inode_info *get_pipe_info(struct file *file, bool for_splice);

int create_pipe_files(struct file **, int);
unsigned int round_pipe_size(unsigned long size);

#endif
