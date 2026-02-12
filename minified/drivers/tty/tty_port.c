#include <linux/spinlock.h>
#include <linux/tty_port.h>
#include <linux/string.h>

void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	mutex_init(&port->mutex);
	mutex_init(&port->buf_mutex);
	spin_lock_init(&port->lock);
	kref_init(&port->kref);
}
