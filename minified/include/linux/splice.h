#ifndef SPLICE_H
#define SPLICE_H

#include <linux/pipe_fs_i.h>

/* SPLICE_F_* macros removed - unused */

struct splice_desc {
	size_t total_len;		 
	unsigned int len;		 
	unsigned int flags;		 
	 
	union {
		void __user *userptr;	 
		struct file *file;	 
		void *data;		 
	} u;
	loff_t pos;			 
	loff_t *opos;			 
	size_t num_spliced;		 
	bool need_wakeup;		 
};

/* splice_actor typedef, splice_from_pipe removed - never called */

extern const struct pipe_buf_operations page_cache_pipe_buf_ops;
extern const struct pipe_buf_operations default_pipe_buf_ops;
#endif
