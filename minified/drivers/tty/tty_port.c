/* linux/errno.h removed - no errno constants used */
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_buffer.h>
#include <linux/tty_port.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/bitops.h>
/* linux/module.h removed - no module features used */

/* tty_port_default_receive_buf, tty_port_default_wakeup removed - client_ops never read */
/* tty_port_default_client_ops removed - only read from removed receive_buf in tty_buffer.c */

void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	/* tty_buffer_init inlined from tty_buffer.c */
	{
		struct tty_bufhead *buf = &port->buf;
		mutex_init(&buf->lock);
		buf->sentinel.size = 0;
		buf->sentinel.next = NULL;
		buf->head = &buf->sentinel;
		buf->tail = &buf->sentinel;
		buf->free.first = NULL;
		atomic_set(&buf->mem_used, 0);
		atomic_set(&buf->priority, 0);
	}
	mutex_init(&port->mutex);
	mutex_init(&port->buf_mutex);
	spin_lock_init(&port->lock);
	/* client_ops not set - never read */
	kref_init(&port->kref);
}

/* tty_port_link_device, tty_port_register_device, tty_port_register_device_attr,
   tty_port_register_device_attr_serdev, tty_port_register_device_serdev,
   tty_port_alloc_xmit_buf, tty_port_free_xmit_buf removed - never called */

/* tty_port_destroy, tty_port_destructor, tty_port_put removed - zero callers
   tty_port_tty_get, tty_port_tty_set removed - zero callers
   tty_port_install removed - zero callers
   tty_port_hangup, tty_port_carrier_raised, tty_port_raise_dtr_rts,
   tty_port_lower_dtr_rts, tty_port_tty_wakeup,
   tty_port_block_til_ready, tty_port_close_start/end, tty_port_close, tty_port_open removed - never called */
