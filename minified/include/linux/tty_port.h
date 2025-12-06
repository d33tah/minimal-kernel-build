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

#define INIT_KFIFO(fifo) \
	(void)sizeof(&(fifo))

static inline void kfifo_init(void *fifo, void *buffer, unsigned int size)
{
	struct __kfifo *kf = fifo;
	kf->in = 0;
	kf->out = 0;
	kf->mask = size - 1;
	kf->esize = 1;
	kf->data = buffer;
}
/* End of inlined kfifo.h content */

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

struct tty_port_client_operations {
	int (*receive_buf)(struct tty_port *port, const unsigned char *, const unsigned char *, size_t);
	void (*write_wakeup)(struct tty_port *port);
};

extern const struct tty_port_client_operations tty_port_default_client_ops;

struct tty_port {
	struct tty_bufhead	buf;
	struct tty_struct	*tty;
	struct tty_struct	*itty;
	const struct tty_port_operations *ops;
	const struct tty_port_client_operations *client_ops;
	spinlock_t		lock;
	int			blocked_open;
	int			count;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	delta_msr_wait;
	unsigned long		flags;
	unsigned long		iflags;
	unsigned char		console:1;
	struct mutex		mutex;
	struct mutex		buf_mutex;
	unsigned char		*xmit_buf;
	DECLARE_KFIFO_PTR(xmit_fifo, unsigned char);
	unsigned int		close_delay;
	unsigned int		closing_wait;
	int			drain_delay;
	struct kref		kref;
	void			*client_data;
};

#define TTY_PORT_INITIALIZED	0	 
#define TTY_PORT_SUSPENDED	1	 
#define TTY_PORT_ACTIVE		2	 

#define TTY_PORT_CTS_FLOW	3	 
#define TTY_PORT_CHECK_CD	4	 
#define TTY_PORT_KOPENED	5	 

void tty_port_init(struct tty_port *port);
void tty_port_link_device(struct tty_port *port, struct tty_driver *driver,
		unsigned index);
struct device *tty_port_register_device(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device);
struct device *tty_port_register_device_attr(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device, void *drvdata,
		const struct attribute_group **attr_grp);
struct device *tty_port_register_device_serdev(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device);
struct device *tty_port_register_device_attr_serdev(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device, void *drvdata,
		const struct attribute_group **attr_grp);
void tty_port_unregister_device(struct tty_port *port,
		struct tty_driver *driver, unsigned index);
int tty_port_alloc_xmit_buf(struct tty_port *port);
void tty_port_free_xmit_buf(struct tty_port *port);
void tty_port_destroy(struct tty_port *port);
void tty_port_put(struct tty_port *port);

static inline struct tty_port *tty_port_get(struct tty_port *port)
{
	if (port && kref_get_unless_zero(&port->kref))
		return port;
	return NULL;
}

static inline bool tty_port_cts_enabled(const struct tty_port *port)
{
	return test_bit(TTY_PORT_CTS_FLOW, &port->iflags);
}

static inline void tty_port_set_cts_flow(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_CTS_FLOW, &port->iflags, val);
}

static inline bool tty_port_active(const struct tty_port *port)
{
	return test_bit(TTY_PORT_ACTIVE, &port->iflags);
}

static inline void tty_port_set_active(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_ACTIVE, &port->iflags, val);
}

static inline bool tty_port_check_carrier(const struct tty_port *port)
{
	return test_bit(TTY_PORT_CHECK_CD, &port->iflags);
}

static inline void tty_port_set_check_carrier(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_CHECK_CD, &port->iflags, val);
}

static inline bool tty_port_suspended(const struct tty_port *port)
{
	return test_bit(TTY_PORT_SUSPENDED, &port->iflags);
}

static inline void tty_port_set_suspended(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_SUSPENDED, &port->iflags, val);
}

static inline bool tty_port_initialized(const struct tty_port *port)
{
	return test_bit(TTY_PORT_INITIALIZED, &port->iflags);
}

static inline void tty_port_set_initialized(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_INITIALIZED, &port->iflags, val);
}

static inline bool tty_port_kopened(const struct tty_port *port)
{
	return test_bit(TTY_PORT_KOPENED, &port->iflags);
}

static inline void tty_port_set_kopened(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_KOPENED, &port->iflags, val);
}

struct tty_struct *tty_port_tty_get(struct tty_port *port);
void tty_port_tty_set(struct tty_port *port, struct tty_struct *tty);
int tty_port_carrier_raised(struct tty_port *port);
void tty_port_raise_dtr_rts(struct tty_port *port);
void tty_port_lower_dtr_rts(struct tty_port *port);
void tty_port_hangup(struct tty_port *port);
void tty_port_tty_hangup(struct tty_port *port, bool check_clocal);
void tty_port_tty_wakeup(struct tty_port *port);
int tty_port_block_til_ready(struct tty_port *port, struct tty_struct *tty,
		struct file *filp);
int tty_port_close_start(struct tty_port *port, struct tty_struct *tty,
		struct file *filp);
void tty_port_close_end(struct tty_port *port, struct tty_struct *tty);
void tty_port_close(struct tty_port *port, struct tty_struct *tty,
		struct file *filp);
int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		struct tty_struct *tty);
int tty_port_open(struct tty_port *port, struct tty_struct *tty,
		struct file *filp);

static inline int tty_port_users(struct tty_port *port)
{
	return port->count + port->blocked_open;
}

#endif
