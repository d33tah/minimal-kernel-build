
#include <linux/tty.h>
#include "tty.h"

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
	init_llist_head(&buf->free);
	atomic_set(&buf->mem_used, 0);
	INIT_WORK(&buf->work, NULL);
}

