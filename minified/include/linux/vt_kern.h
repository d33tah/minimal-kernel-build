#ifndef _VT_KERN_H
#define _VT_KERN_H

#define MIN_NR_CONSOLES 1
#define MAX_NR_CONSOLES	2

#ifndef KD_TEXT
#define KD_TEXT		0x00
#define KD_GRAPHICS	0x01
#endif

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
/* notifier.h inlined */
#include <linux/rwsem.h>
#include <linux/srcu.h>
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
struct tty_driver;
struct tty_operations;

struct tty_bufhead {
	int dummy;
};

struct tty_port {
	struct tty_bufhead	buf;
	spinlock_t		lock;
	struct mutex		mutex;
	struct mutex		buf_mutex;
	struct kref		kref;
};

static inline void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	mutex_init(&port->mutex);
	mutex_init(&port->buf_mutex);
	spin_lock_init(&port->lock);
	kref_init(&port->kref);
}

struct tty_struct {
	struct kref kref;
} __randomize_layout;

struct uni_pagedir;

struct vc_state {
	unsigned int	x, y;
	unsigned char	color;
};

struct vc_data {
	struct tty_port port;

	struct vc_state state;

	unsigned short	vc_num;
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
	unsigned char	vc_mode;
	unsigned char	vc_attr;
	unsigned char	vc_def_color;
	unsigned short	vc_complement_mask;
	unsigned short	vc_s_complement_mask;
	unsigned long	vc_pos;
	unsigned short	vc_video_erase_char;
	unsigned int	vc_state;
	unsigned int	vc_need_wrap	: 1;
	unsigned int	vc_can_do_color	: 1;
	struct vc_data **vc_display_fg;
	struct uni_pagedir *vc_uni_pagedir;
	struct uni_pagedir **vc_uni_pagedir_loc;
};

struct vc {
	struct vc_data *d;
};

bool con_is_visible(const struct vc_data *vc);
int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int lines);
#endif
