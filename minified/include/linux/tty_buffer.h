#ifndef _LINUX_TTY_BUFFER_H
#define _LINUX_TTY_BUFFER_H

#include <linux/atomic.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct tty_buffer { union { struct tty_buffer *next; struct llist_node free; }; int size; unsigned long data[]; };


struct tty_bufhead { struct tty_buffer *head; struct work_struct work; struct mutex	   lock; struct tty_buffer sentinel; struct llist_head free; atomic_t	   mem_used; };

#endif
