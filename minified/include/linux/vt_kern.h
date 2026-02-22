#ifndef _VT_KERN_H
#define _VT_KERN_H

#define VT_SINGLE_DRIVER
#define MIN_NR_CONSOLES 1
#define MAX_NR_CONSOLES	1

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H
struct notifier_block;
typedef	int (*notifier_fn_t)(struct notifier_block *nb,
			unsigned long action, void *data);
struct notifier_block {
	notifier_fn_t notifier_call;
	struct notifier_block __rcu *next;
	int priority;
};
#define NOTIFY_DONE		0x0000
#endif

struct tty_struct;

struct tty_port {
	spinlock_t		lock;
	struct mutex		mutex;
	struct kref		kref;
};

static inline void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	mutex_init(&port->mutex);
	spin_lock_init(&port->lock);
	kref_init(&port->kref);
}

struct vc_state {
	unsigned int	x, y;
	unsigned char	color;
};

struct vc_data {
	struct tty_port port;

	struct vc_state state;

	unsigned int	vc_cols;
	unsigned int	vc_rows;
	unsigned int	vc_size_row;
	unsigned int	vc_cell_height;
	unsigned long	vc_origin;
	unsigned long	vc_scr_end;
	unsigned long	vc_visible_origin;
	unsigned int	vc_top, vc_bottom;
	const struct consw *vc_sw;
	unsigned short	*vc_screenbuf;
	unsigned int	vc_screenbuf_size;
	unsigned char	vc_attr;
	unsigned char	vc_def_color;
	unsigned long	vc_pos;
	unsigned short	vc_video_erase_char;
	unsigned int	vc_state;
	unsigned int	vc_need_wrap	: 1;
};

struct vc {
	struct vc_data *d;
};

static inline bool con_is_visible(const struct vc_data *vc) { return true; }
int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int lines);
#endif
