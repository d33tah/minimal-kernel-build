
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "tty.h"

void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	tty_buffer_init(port);
	kref_init(&port->kref);
}

void tty_port_put(struct tty_port *port)
{
	/* Runtime-dead teardown root: a tty_port is released only when its last
	 * kref drops (final close / vc teardown), which never happens on a
	 * single-shot boot. Symbol kept link-live for the tty_port.h extern and
	 * the (dead) vt.c hangup caller. The private tty_port_destructor release
	 * callback and its tty_buffer_cancel_work / tty_buffer_free_all callees
	 * were deleted with it. */
}

int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		struct tty_struct *tty)
{
	tty->port = port;
	return tty_standard_install(driver, tty);
}
