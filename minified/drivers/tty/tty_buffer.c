
#include <linux/types.h>
/* linux/errno.h removed - no errno constants used */
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_buffer.h>
#include <linux/tty_port.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/bitops.h>
/* linux/module.h removed - no module features used */
#include <linux/ratelimit.h>
#include "tty.h"

/* TTYB_DEFAULT_MEM_LIMIT removed - mem_limit field removed */

static void tty_buffer_reset(struct tty_buffer *p, size_t size)
{
	/* used, commit, read, flags removed - write-only, never read */
	p->size = size;
	p->next = NULL;
}

void tty_buffer_free_all(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;
	struct tty_buffer *p, *next;
	struct llist_node *llist;
	unsigned int freed = 0;
	int still_used;

	while ((p = buf->head) != NULL) {
		buf->head = p->next;
		freed += p->size;
		if (p->size > 0)
			kfree(p);
	}
	llist = llist_del_all(&buf->free);
	llist_for_each_entry_safe(p, next, llist, free)
		kfree(p);

	tty_buffer_reset(&buf->sentinel, 0);
	buf->head = &buf->sentinel;
	buf->tail = &buf->sentinel;

	still_used = atomic_xchg(&buf->mem_used, 0);
	WARN(still_used != freed, "we still have not freed %d bytes!",
	     still_used - freed);
}

/* tty_ldisc_receive_buf removed - only called via flush_to_ldisc which was never triggered */

void tty_buffer_init(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;

	mutex_init(&buf->lock);
	tty_buffer_reset(&buf->sentinel, 0);
	buf->head = &buf->sentinel;
	buf->tail = &buf->sentinel;
	buf->free.first = NULL; /* init_llist_head inlined */
	atomic_set(&buf->mem_used, 0);
	atomic_set(&buf->priority, 0);
	/* buf->work not initialized - tty_flip_buffer_push/tty_buffer_restart_work never called */
	/* buf->mem_limit assignment removed - field removed */
}

/* tty_buffer_cancel_work moved to tty.h as static inline */
