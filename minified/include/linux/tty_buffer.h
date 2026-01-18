#ifndef _LINUX_TTY_BUFFER_H
#define _LINUX_TTY_BUFFER_H

#include <linux/atomic.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct tty_buffer {
	union {
		struct tty_buffer *next;
		struct llist_node free;
	};
	int used;
	int size;
	int commit;
	int read;
	int flags;
	 
	unsigned long data[];
};

/* TTYB_NORMAL removed - never used */
/* char_buf_ptr, flag_buf_ptr removed - never called */

struct tty_bufhead {
	struct tty_buffer *head;	 
	struct work_struct work;
	struct mutex	   lock;
	atomic_t	   priority;
	struct tty_buffer sentinel;
	struct llist_head free;		 
	atomic_t	   mem_used;     
	int		   mem_limit;
	struct tty_buffer *tail;	 
};

/* TTY_NORMAL removed - never used */

#endif
