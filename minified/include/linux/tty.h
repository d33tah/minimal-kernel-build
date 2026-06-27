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
#include <linux/seq_file.h>

struct ld_semaphore {
	atomic_long_t		count;
};

void __init_ldsem(struct ld_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_ldsem(sem)						\
do {								\
	static struct lock_class_key __key;			\
								\
	__init_ldsem((sem), #sem, &__key);			\
} while (0)

int ldsem_down_read(struct ld_semaphore *sem, long timeout);
int ldsem_down_write(struct ld_semaphore *sem, long timeout);
void ldsem_up_read(struct ld_semaphore *sem);
void ldsem_up_write(struct ld_semaphore *sem);

# define ldsem_down_write_nested(sem, subclass, timeout)	\
		ldsem_down_write(sem, timeout)

struct tty_ldisc_ops {
	int	num;
	int	(*open)(struct tty_struct *tty);
	void	(*close)(struct tty_struct *tty);
	/* flush_buffer, read, set_termios, hangup, dcd_change removed -
	 * these ldisc callbacks were never dispatched (no ld->ops->* call) */
	ssize_t	(*write)(struct tty_struct *tty, struct file *file,
			 const unsigned char *buf, size_t nr);
};

struct tty_ldisc {
	struct tty_ldisc_ops *ops;
	struct tty_struct *tty;
};


void tty_ldisc_deref(struct tty_ldisc *);
struct tty_ldisc *tty_ldisc_ref_wait(struct tty_struct *);

int tty_register_ldisc(struct tty_ldisc_ops *new_ldisc);
#include <linux/mutex.h>
#include <linux/rwsem.h>

/* From uapi/linux/tty.h - reduced to only used values */
#define N_TTY		0
#define NR_LDISCS	31
#include <linux/llist.h>

struct device;
struct signal_struct;
struct tty_operations;

struct tty_struct {
	int	magic;
	struct kref kref;
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

#define TTY_IO_ERROR		1
#define TTY_LDISC_OPEN		11
#define TTY_NO_WRITE_SPLIT	17

static inline bool tty_io_error(struct tty_struct *tty)
{
	return test_bit(TTY_IO_ERROR, &tty->flags);
}

void tty_kref_put(struct tty_struct *tty);
int __init tty_init(void);
const char *tty_name(const struct tty_struct *tty);

extern struct ktermios tty_std_termios;

static inline int vcs_init(void) { return 0; }

extern struct class *tty_class;


static inline struct tty_struct *tty_kref_get(struct tty_struct *tty)
{
	if (tty)
		kref_get(&tty->kref);
	return tty;
}

const char *tty_driver_name(const struct tty_struct *tty);
speed_t tty_termios_baud_rate(struct ktermios *termios);



struct tty_struct *tty_init_dev(struct tty_driver *driver, int idx);
void tty_init_termios(struct tty_struct *tty);
int tty_standard_install(struct tty_driver *driver,
		struct tty_struct *tty);

extern struct mutex tty_mutex;

void __init n_tty_init(void);

void tty_lock(struct tty_struct *tty);
int  tty_lock_interruptible(struct tty_struct *tty);
void tty_unlock(struct tty_struct *tty);

#endif
