/* --- 2026-02-06 22:45 --- Gutted: most of TTY subsystem dead after
 * tty_io.c removal. Keep struct tty_struct (for fork.c tty_kref_get),
 * tty_kref_put (for exit.c), and n_tty_init/tty_register_ldisc. */
#ifndef _LINUX_TTY_H
#define _LINUX_TTY_H

#include <linux/fs.h>
#include <linux/major.h>
#include <linux/types.h>
#include <asm/termios.h>
#include <linux/workqueue.h>
#include <linux/tty_buffer.h>
#include <linux/tty_driver.h>
#include <linux/tty_port.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/llist.h>

struct ld_semaphore {
	atomic_long_t		count;
	raw_spinlock_t		wait_lock;
	unsigned int		wait_readers;
	struct list_head	read_wait;
	struct list_head	write_wait;
};

/* ldsem functions removed - tty_ldsem.c deleted, no callers */

struct tty_ldisc_ops {
	char	*name;
	int	num;
	struct  module *owner;
};

struct tty_ldisc {
	struct tty_ldisc_ops *ops;
	struct tty_struct *tty;
};

int tty_register_ldisc(struct tty_ldisc_ops *new_ldisc);

#define N_TTY		0
#define NR_LDISCS	31

struct device;
struct tty_operations;

struct tty_struct {
	int	magic;
	struct kref kref;
	struct device *dev;
	struct tty_driver *driver;
	const struct tty_operations *ops;
	int index;

	struct ld_semaphore ldisc_sem;
	struct tty_ldisc *ldisc;

	struct mutex atomic_write_lock;
	struct mutex legacy_mutex;
	struct rw_semaphore termios_rwsem;
	struct ktermios termios;
	char name[64];
	unsigned long flags;
	int count;
	struct winsize winsize;

	struct {
		spinlock_t lock;
		bool stopped;
		unsigned long unused[0];
	} __aligned(sizeof(unsigned long)) flow;

	struct {
		spinlock_t lock;
		struct pid *pgrp;
		struct pid *session;
		unsigned long unused[0];
	} __aligned(sizeof(unsigned long)) ctrl;

	struct tty_struct *link;
	wait_queue_head_t write_wait;
	wait_queue_head_t read_wait;
	struct work_struct hangup_work;
	void *disc_data;
	void *driver_data;
	spinlock_t files_lock;
	struct list_head tty_files;
	unsigned char *write_buf;
	int write_cnt;
	struct tty_port *port;
} __randomize_layout;

struct tty_file_private {
	struct tty_struct *tty;
	struct list_head list;
};

#define TTY_MAGIC		0x5401

void tty_kref_put(struct tty_struct *tty);

/* extern tty_class removed - never used outside tty_io.c */

void __init n_tty_init(void);

#endif
