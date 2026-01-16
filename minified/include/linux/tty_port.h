#ifndef _LINUX_TTY_PORT_H
#define _LINUX_TTY_PORT_H

/* Inlined from kfifo.h */
struct __kfifo {
	unsigned int	in;
	unsigned int	out;
	unsigned int	mask;
	unsigned int	esize;
	void		*data;
};

#define __STRUCT_KFIFO_COMMON(datatype, recsize, ptrtype) \
	union { \
		struct __kfifo	kfifo; \
		datatype	*type; \
		const datatype	*const_type; \
		char		(*rectype)[recsize]; \
		ptrtype		*ptr; \
		ptrtype const	*ptr_const; \
	}

#define __STRUCT_KFIFO_PTR(type, recsize, ptrtype) \
{ \
	__STRUCT_KFIFO_COMMON(type, recsize, ptrtype); \
	type		buf[0]; \
}

#define DECLARE_KFIFO_PTR(fifo, type)	\
struct __STRUCT_KFIFO_PTR(type, 0, type) fifo

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

/* tty_port_client_operations struct removed - never read after tty_buffer.c receive_buf removed */
/* tty_port_default_client_ops removed - never read */

struct tty_port {
	struct tty_bufhead	buf;
	struct tty_struct	*tty;
	struct tty_struct	*itty;
	const struct tty_port_operations *ops;
	/* client_ops field removed - never read */
	spinlock_t		lock;
	/* blocked_open, open_wait, delta_msr_wait removed - never used */
	int			count;
	unsigned long		flags;
	unsigned long		iflags;
	unsigned char		console:1;
	struct mutex		mutex;
	struct mutex		buf_mutex;
	unsigned char		*xmit_buf;
	DECLARE_KFIFO_PTR(xmit_fifo, unsigned char);
	/* close_delay, closing_wait, drain_delay, client_data removed - never read */
	struct kref		kref;
};

#define TTY_PORT_KOPENED	5	 

void tty_port_init(struct tty_port *port);
/* tty_port_link_device, tty_port_register_device, tty_port_register_device_attr,
   tty_port_register_device_serdev, tty_port_alloc_xmit_buf, tty_port_free_xmit_buf,
   tty_port_register_device_attr_serdev, tty_port_unregister_device removed - never called */
void tty_port_destroy(struct tty_port *port);
void tty_port_put(struct tty_port *port);

static inline struct tty_port *tty_port_get(struct tty_port *port)
{
	if (port && kref_get_unless_zero(&port->kref))
		return port;
	return NULL;
}


/* tty_port_set_active removed - never called */

static inline bool tty_port_kopened(const struct tty_port *port)
{
	return test_bit(TTY_PORT_KOPENED, &port->iflags);
}


struct tty_struct *tty_port_tty_get(struct tty_port *port);
void tty_port_tty_set(struct tty_port *port, struct tty_struct *tty);
/* tty_port_carrier_raised, tty_port_raise_dtr_rts, tty_port_lower_dtr_rts,
   tty_port_hangup, tty_port_tty_wakeup,
   tty_port_block_til_ready, tty_port_close_start/end, tty_port_close, tty_port_open removed */
int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		struct tty_struct *tty);


#endif
