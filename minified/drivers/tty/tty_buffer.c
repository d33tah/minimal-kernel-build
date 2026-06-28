
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include "tty.h"

#define MIN_TTYB_SIZE	256
#define TTYB_ALIGN_MASK	255

#define TTY_BUFFER_PAGE	(((PAGE_SIZE - sizeof(struct tty_buffer)) / 2) & ~0xFF)


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

