#include <linux/vt_kern.h>
#include <linux/string.h>

void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	mutex_init(&port->mutex);
	mutex_init(&port->buf_mutex);
	spin_lock_init(&port->lock);
	kref_init(&port->kref);
}
