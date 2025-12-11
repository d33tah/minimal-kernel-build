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

/* --- 2025-12-06 17:12 --- tty_ldisc.h inlined */
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/seq_file.h>

struct ld_semaphore {
	atomic_long_t		count;
	raw_spinlock_t		wait_lock;
	unsigned int		wait_readers;
	struct list_head	read_wait;
	struct list_head	write_wait;
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
int ldsem_down_read_trylock(struct ld_semaphore *sem);
int ldsem_down_write(struct ld_semaphore *sem, long timeout);
int ldsem_down_write_trylock(struct ld_semaphore *sem);
void ldsem_up_read(struct ld_semaphore *sem);
void ldsem_up_write(struct ld_semaphore *sem);

# define ldsem_down_read_nested(sem, subclass, timeout)		\
		ldsem_down_read(sem, timeout)
# define ldsem_down_write_nested(sem, subclass, timeout)	\
		ldsem_down_write(sem, timeout)

struct tty_ldisc_ops {
	char	*name;
	int	num;
	int	(*open)(struct tty_struct *tty);
	void	(*close)(struct tty_struct *tty);
	void	(*flush_buffer)(struct tty_struct *tty);
	ssize_t	(*read)(struct tty_struct *tty, struct file *file,
			unsigned char *buf, size_t nr,
			void **cookie, unsigned long offset);
	ssize_t	(*write)(struct tty_struct *tty, struct file *file,
			 const unsigned char *buf, size_t nr);
	int	(*ioctl)(struct tty_struct *tty, unsigned int cmd,
			unsigned long arg);
	int	(*compat_ioctl)(struct tty_struct *tty, unsigned int cmd,
			unsigned long arg);
	void	(*set_termios)(struct tty_struct *tty, struct ktermios *old);
	__poll_t (*poll)(struct tty_struct *tty, struct file *file,
			     struct poll_table_struct *wait);
	void	(*hangup)(struct tty_struct *tty);
	void	(*receive_buf)(struct tty_struct *tty, const unsigned char *cp,
			       const char *fp, int count);
	void	(*write_wakeup)(struct tty_struct *tty);
	void	(*dcd_change)(struct tty_struct *tty, unsigned int status);
	int	(*receive_buf2)(struct tty_struct *tty, const unsigned char *cp,
				const char *fp, int count);
	struct  module *owner;
};

struct tty_ldisc {
	struct tty_ldisc_ops *ops;
	struct tty_struct *tty;
};

#define MODULE_ALIAS_LDISC(ldisc) \
	MODULE_ALIAS("tty-ldisc-" __stringify(ldisc))

extern const struct seq_operations tty_ldiscs_seq_ops;

struct tty_ldisc *tty_ldisc_ref(struct tty_struct *);
void tty_ldisc_deref(struct tty_ldisc *);
struct tty_ldisc *tty_ldisc_ref_wait(struct tty_struct *);

void tty_ldisc_flush(struct tty_struct *tty);

int tty_register_ldisc(struct tty_ldisc_ops *new_ldisc);
void tty_unregister_ldisc(struct tty_ldisc_ops *ldisc);
int tty_set_ldisc(struct tty_struct *tty, int disc);
/* --- end tty_ldisc.h inlined --- */
#include <linux/mutex.h>
#include <linux/tty_flags.h>
#include <linux/rwsem.h>

/* From uapi/linux/tty.h - reduced to only used values */
#define N_TTY		0
#define N_NULL		27
#define NR_LDISCS	31
#include <linux/llist.h>


/* NR_UNIX98_PTY_* defines removed - no PTY support in minimal kernel */

#define __DISABLED_CHAR '\0'

/* *_CHAR macros removed - unused */

/* Flag accessor macros - only keep used ones */
#define _C_FLAG(tty, f)	((tty)->termios.c_cflag & (f))
#define _L_FLAG(tty, f)	((tty)->termios.c_lflag & (f))

/* C_* macros - keep only used: C_BAUD, C_HUPCL, C_CLOCAL */
#define C_BAUD(tty)	_C_FLAG((tty), CBAUD)
#define C_HUPCL(tty)	_C_FLAG((tty), HUPCL)
#define C_CLOCAL(tty)	_C_FLAG((tty), CLOCAL)

/* L_* macros - keep only used: L_TOSTOP */
#define L_TOSTOP(tty)	_L_FLAG((tty), TOSTOP)

struct device;
struct signal_struct;
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
	struct mutex throttle_mutex;
	struct rw_semaphore termios_rwsem;
	struct mutex winsize_mutex;
	struct ktermios termios, termios_locked;
	char name[64];
	unsigned long flags;
	int count;
	struct winsize winsize;

	struct {
		spinlock_t lock;
		bool stopped;
		bool tco_stopped;
		unsigned long unused[0];
	} __aligned(sizeof(unsigned long)) flow;

	struct {
		spinlock_t lock;
		struct pid *pgrp;
		struct pid *session;
		unsigned char pktstatus;
		bool packet;
		unsigned long unused[0];
	} __aligned(sizeof(unsigned long)) ctrl;

	int hw_stopped;
	unsigned int receive_room;
	int flow_change;

	struct tty_struct *link;
	struct fasync_struct *fasync;
	wait_queue_head_t write_wait;
	wait_queue_head_t read_wait;
	struct work_struct hangup_work;
	void *disc_data;
	void *driver_data;
	spinlock_t files_lock;
	struct list_head tty_files;

#define N_TTY_BUF_SIZE 4096

	int closing;
	unsigned char *write_buf;
	int write_cnt;
	struct work_struct SAK_work;
	struct tty_port *port;
} __randomize_layout;

struct tty_file_private {
	struct tty_struct *tty;
	struct file *file;
	struct list_head list;
};

#define TTY_MAGIC		0x5401

#define TTY_THROTTLED		0
#define TTY_IO_ERROR		1
#define TTY_OTHER_CLOSED	2
#define TTY_EXCLUSIVE		3
#define TTY_DO_WRITE_WAKEUP	5
#define TTY_LDISC_OPEN		11
#define TTY_PTY_LOCK		16
#define TTY_NO_WRITE_SPLIT	17
#define TTY_HUPPED		18
#define TTY_HUPPING		19
#define TTY_LDISC_CHANGING	20
#define TTY_LDISC_HALTED	22

static inline bool tty_io_error(struct tty_struct *tty)
{
	return test_bit(TTY_IO_ERROR, &tty->flags);
}

static inline bool tty_throttled(struct tty_struct *tty)
{
	return test_bit(TTY_THROTTLED, &tty->flags);
}

void tty_kref_put(struct tty_struct *tty);
struct pid *tty_get_pgrp(struct tty_struct *tty);
void tty_vhangup_self(void);
void disassociate_ctty(int priv);
dev_t tty_devnum(struct tty_struct *tty);
void proc_clear_tty(struct task_struct *p);
struct tty_struct *get_current_tty(void);
int __init tty_init(void);
const char *tty_name(const struct tty_struct *tty);
struct tty_struct *tty_kopen_exclusive(dev_t device);
struct tty_struct *tty_kopen_shared(dev_t device);
void tty_kclose(struct tty_struct *tty);
int tty_dev_name_to_number(const char *name, dev_t *number);

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
void tty_wait_until_sent(struct tty_struct *tty, long timeout);
/* stop_tty, start_tty, tty_write_message, tty_send_xchar removed - unused */
int tty_put_char(struct tty_struct *tty, unsigned char c);
unsigned int tty_chars_in_buffer(struct tty_struct *tty);
unsigned int tty_write_room(struct tty_struct *tty);
void tty_driver_flush_buffer(struct tty_struct *tty);
void tty_unthrottle(struct tty_struct *tty);
int tty_throttle_safe(struct tty_struct *tty);
int tty_unthrottle_safe(struct tty_struct *tty);
int tty_do_resize(struct tty_struct *tty, struct winsize *ws);
int tty_get_icount(struct tty_struct *tty,
		struct serial_icounter_struct *icount);
int is_current_pgrp_orphaned(void);
void tty_hangup(struct tty_struct *tty);
void tty_vhangup(struct tty_struct *tty);
int tty_hung_up_p(struct file *filp);
void do_SAK(struct tty_struct *tty);
void __do_SAK(struct tty_struct *tty);
void no_tty(void);
speed_t tty_termios_baud_rate(struct ktermios *termios);

static inline speed_t tty_get_baud_rate(struct tty_struct *tty)
{
	return tty_termios_baud_rate(&tty->termios);
}

unsigned char tty_get_char_size(unsigned int cflag);
unsigned char tty_get_frame_size(unsigned int cflag);

void tty_termios_copy_hw(struct ktermios *new, struct ktermios *old);
int tty_termios_hw_change(const struct ktermios *a, const struct ktermios *b);
int tty_set_termios(struct tty_struct *tty, struct ktermios *kt);

void tty_wakeup(struct tty_struct *tty);

int tty_mode_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);
int tty_perform_flush(struct tty_struct *tty, unsigned long arg);
struct tty_struct *tty_init_dev(struct tty_driver *driver, int idx);
void tty_release_struct(struct tty_struct *tty, int idx);
void tty_init_termios(struct tty_struct *tty);
void tty_save_termios(struct tty_struct *tty);
int tty_standard_install(struct tty_driver *driver,
		struct tty_struct *tty);

extern struct mutex tty_mutex;

void n_tty_inherit_ops(struct tty_ldisc_ops *ops);
void __init n_tty_init(void);

static inline void tty_audit_exit(void)
{
}
static inline void tty_audit_fork(struct signal_struct *sig)
{
}
int n_tty_ioctl_helper(struct tty_struct *tty, unsigned int cmd,
		unsigned long arg);


int vt_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);

long vt_compat_ioctl(struct tty_struct *tty, unsigned int cmd,
		unsigned long arg);

void tty_lock(struct tty_struct *tty);
int  tty_lock_interruptible(struct tty_struct *tty);
void tty_unlock(struct tty_struct *tty);
void tty_lock_slave(struct tty_struct *tty);
void tty_unlock_slave(struct tty_struct *tty);
void tty_set_lock_subclass(struct tty_struct *tty);

#endif
