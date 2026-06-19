#ifndef _LINUX_TTY_PORT_H
#define _LINUX_TTY_PORT_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/tty_buffer.h>
#include <linux/wait.h>

struct attribute_group;
struct tty_driver;
struct tty_port;
struct tty_struct;

struct tty_port_operations {
	int (*carrier_raised)(struct tty_port *port);
	void (*dtr_rts)(struct tty_port *port, int raise);
	void (*shutdown)(struct tty_port *port);
	int (*activate)(struct tty_port *port, struct tty_struct *tty);
	void (*destruct)(struct tty_port *port);
};

struct tty_port {
	struct tty_bufhead	buf;
	struct tty_struct	*tty;
	struct tty_struct	*itty;
	const struct tty_port_operations *ops;
	spinlock_t		lock;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	delta_msr_wait;
	unsigned long		iflags;
	struct mutex		mutex;
	struct mutex		buf_mutex;
	unsigned char		*xmit_buf;
	struct kref		kref;
};

#define TTY_PORT_INITIALIZED	0	 
#define TTY_PORT_SUSPENDED	1	 
#define TTY_PORT_ACTIVE		2	 

#define TTY_PORT_CTS_FLOW	3	 
#define TTY_PORT_CHECK_CD	4	 
#define TTY_PORT_KOPENED	5	 

void tty_port_init(struct tty_port *port);
void tty_port_destroy(struct tty_port *port);
void tty_port_put(struct tty_port *port);

static inline struct tty_port *tty_port_get(struct tty_port *port)
{
	if (port && kref_get_unless_zero(&port->kref))
		return port;
	return NULL;
}



static inline bool tty_port_kopened(const struct tty_port *port)
{
	return test_bit(TTY_PORT_KOPENED, &port->iflags);
}


int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		struct tty_struct *tty);


#endif
