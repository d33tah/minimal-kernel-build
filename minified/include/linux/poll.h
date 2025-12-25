#ifndef _LINUX_POLL_H
#define _LINUX_POLL_H


#include <linux/compiler.h>
#include <linux/ktime.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/eventpoll.h>

/* Inlined from uapi/asm-generic/poll.h - needed by poll maptopoll */
#define POLLIN		0x0001
#define POLLPRI		0x0002
#define POLLOUT		0x0004
#define POLLERR		0x0008
#define POLLHUP		0x0010
#define POLLNVAL	0x0020
#define POLLRDNORM	0x0040
#define POLLRDBAND	0x0080
#define POLLWRNORM	0x0100
#define POLLWRBAND	0x0200
#define POLLMSG		0x0400
#define POLLRDHUP       0x2000

#define POLLFREE	(__force __poll_t)0x4000
#define POLL_BUSY_LOOP	(__force __poll_t)0x8000

struct pollfd {
	int fd;
	short events;
	short revents;
};

#ifdef __clang__
#define MAX_STACK_ALLOC 768
#else
#define MAX_STACK_ALLOC 832
#endif
#define FRONTEND_STACK_ALLOC	256
#define SELECT_STACK_ALLOC	FRONTEND_STACK_ALLOC
#define POLL_STACK_ALLOC	FRONTEND_STACK_ALLOC
#define WQUEUES_STACK_ALLOC	(MAX_STACK_ALLOC - FRONTEND_STACK_ALLOC)
#define N_INLINE_POLL_ENTRIES	(WQUEUES_STACK_ALLOC / sizeof(struct poll_table_entry))

#define DEFAULT_POLLMASK (EPOLLIN | EPOLLOUT | EPOLLRDNORM | EPOLLWRNORM)

struct poll_table_struct;

typedef void (*poll_queue_proc)(struct file *, wait_queue_head_t *, struct poll_table_struct *);

typedef struct poll_table_struct {
	poll_queue_proc _qproc;
	__poll_t _key;
} poll_table;

static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	if (p && p->_qproc && wait_address)
		p->_qproc(filp, wait_address, p);
}


struct poll_table_entry {
	struct file *filp;
	__poll_t key;
	wait_queue_entry_t wait;
	wait_queue_head_t *wait_address;
};

#define MAX_INT64_SECONDS (((s64)(~((u64)0)>>1)/HZ)-1)

/* core_sys_select, poll_select_set_timeout removed - declared but never implemented */

#endif  
