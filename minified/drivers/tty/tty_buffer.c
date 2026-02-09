/* --- 2026-02-06 22:30 --- Gutted: tty_buffer_free_all had no callers.
 * Keep only tty_buffer_init called from tty_port_init (used by vt.c). */

#include <linux/types.h>
#include <linux/tty.h>
#include <linux/tty_buffer.h>
#include <linux/tty_port.h>
#include <linux/slab.h>

static void tty_buffer_reset(struct tty_buffer *p, size_t size)
{
	p->size = size;
	p->next = NULL;
}

void tty_buffer_init(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;

	mutex_init(&buf->lock);
	tty_buffer_reset(&buf->sentinel, 0);
	buf->head = &buf->sentinel;
	buf->tail = &buf->sentinel;
	buf->free.first = NULL;
	atomic_set(&buf->mem_used, 0);
	atomic_set(&buf->priority, 0);
}
