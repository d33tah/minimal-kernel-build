#ifndef _LINUX_TTY_H
#define _LINUX_TTY_H

#include <linux/fs.h>
#include <linux/major.h>
#include <linux/types.h>
#include <asm/termios.h>
#include <linux/workqueue.h>
#include <linux/tty_buffer.h>
#include <linux/tty_driver.h>
#include <linux/tty_ldisc.h>
#include <linux/tty_port.h>
#include <linux/mutex.h>
#include <linux/tty_flags.h>
#include <linux/rwsem.h>

/* From uapi/linux/tty.h - inlined */
#define N_TTY		0
#define N_SLIP		1
#define N_MOUSE		2
#define N_PPP		3
#define N_STRIP		4
#define N_AX25		5
#define N_X25		6
#define N_6PACK		7
#define N_MASC		8
#define N_R3964		9
#define N_PROFIBUS_FDL	10
#define N_IRDA		11
#define N_SMSBLOCK	12
#define N_HDLC		13
#define N_SYNC_PPP	14
#define N_HCI		15
#define N_GIGASET_M101	16
#define N_SLCAN		17
#define N_PPS		18
#define N_V253		19
#define N_CAIF		20
#define N_GSM0710	21
#define N_TI_WL		22
#define N_TRACESINK	23
#define N_TRACEROUTER	24
#define N_NCI		25
#define N_SPEAKUP	26
#define N_NULL		27
#define N_MCTP		28
#define N_DEVELOPMENT	29
#define N_CAN327	30
#define NR_LDISCS	31
#include <linux/llist.h>


#define NR_UNIX98_PTY_DEFAULT	4096       
#define NR_UNIX98_PTY_RESERVE	1024	   
#define NR_UNIX98_PTY_MAX	(1 << MINORBITS)  

#define __DISABLED_CHAR '\0'

#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE])
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])
#define TIME_CHAR(tty) ((tty)->termios.c_cc[VTIME])
#define MIN_CHAR(tty) ((tty)->termios.c_cc[VMIN])
#define SWTC_CHAR(tty) ((tty)->termios.c_cc[VSWTC])
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])
#define SUSP_CHAR(tty) ((tty)->termios.c_cc[VSUSP])
#define EOL_CHAR(tty) ((tty)->termios.c_cc[VEOL])
#define REPRINT_CHAR(tty) ((tty)->termios.c_cc[VREPRINT])
#define DISCARD_CHAR(tty) ((tty)->termios.c_cc[VDISCARD])
#define WERASE_CHAR(tty) ((tty)->termios.c_cc[VWERASE])
#define LNEXT_CHAR(tty)	((tty)->termios.c_cc[VLNEXT])
#define EOL2_CHAR(tty) ((tty)->termios.c_cc[VEOL2])

#define _I_FLAG(tty, f)	((tty)->termios.c_iflag & (f))
#define _O_FLAG(tty, f)	((tty)->termios.c_oflag & (f))
#define _C_FLAG(tty, f)	((tty)->termios.c_cflag & (f))
#define _L_FLAG(tty, f)	((tty)->termios.c_lflag & (f))

#define I_IGNBRK(tty)	_I_FLAG((tty), IGNBRK)
#define I_BRKINT(tty)	_I_FLAG((tty), BRKINT)
#define I_IGNPAR(tty)	_I_FLAG((tty), IGNPAR)
#define I_PARMRK(tty)	_I_FLAG((tty), PARMRK)
#define I_INPCK(tty)	_I_FLAG((tty), INPCK)
#define I_ISTRIP(tty)	_I_FLAG((tty), ISTRIP)
#define I_INLCR(tty)	_I_FLAG((tty), INLCR)
#define I_IGNCR(tty)	_I_FLAG((tty), IGNCR)
#define I_ICRNL(tty)	_I_FLAG((tty), ICRNL)
#define I_IUCLC(tty)	_I_FLAG((tty), IUCLC)
#define I_IXON(tty)	_I_FLAG((tty), IXON)
#define I_IXANY(tty)	_I_FLAG((tty), IXANY)
#define I_IXOFF(tty)	_I_FLAG((tty), IXOFF)
#define I_IMAXBEL(tty)	_I_FLAG((tty), IMAXBEL)
#define I_IUTF8(tty)	_I_FLAG((tty), IUTF8)

#define O_OPOST(tty)	_O_FLAG((tty), OPOST)
#define O_OLCUC(tty)	_O_FLAG((tty), OLCUC)
#define O_ONLCR(tty)	_O_FLAG((tty), ONLCR)
#define O_OCRNL(tty)	_O_FLAG((tty), OCRNL)
#define O_ONOCR(tty)	_O_FLAG((tty), ONOCR)
#define O_ONLRET(tty)	_O_FLAG((tty), ONLRET)
#define O_OFILL(tty)	_O_FLAG((tty), OFILL)
#define O_OFDEL(tty)	_O_FLAG((tty), OFDEL)
#define O_NLDLY(tty)	_O_FLAG((tty), NLDLY)
#define O_CRDLY(tty)	_O_FLAG((tty), CRDLY)
#define O_TABDLY(tty)	_O_FLAG((tty), TABDLY)
#define O_BSDLY(tty)	_O_FLAG((tty), BSDLY)
#define O_VTDLY(tty)	_O_FLAG((tty), VTDLY)
#define O_FFDLY(tty)	_O_FLAG((tty), FFDLY)

#define C_BAUD(tty)	_C_FLAG((tty), CBAUD)
#define C_CSIZE(tty)	_C_FLAG((tty), CSIZE)
#define C_CSTOPB(tty)	_C_FLAG((tty), CSTOPB)
#define C_CREAD(tty)	_C_FLAG((tty), CREAD)
#define C_PARENB(tty)	_C_FLAG((tty), PARENB)
#define C_PARODD(tty)	_C_FLAG((tty), PARODD)
#define C_HUPCL(tty)	_C_FLAG((tty), HUPCL)
#define C_CLOCAL(tty)	_C_FLAG((tty), CLOCAL)
#define C_CIBAUD(tty)	_C_FLAG((tty), CIBAUD)
#define C_CRTSCTS(tty)	_C_FLAG((tty), CRTSCTS)
#define C_CMSPAR(tty)	_C_FLAG((tty), CMSPAR)

#define L_ISIG(tty)	_L_FLAG((tty), ISIG)
#define L_ICANON(tty)	_L_FLAG((tty), ICANON)
#define L_XCASE(tty)	_L_FLAG((tty), XCASE)
#define L_ECHO(tty)	_L_FLAG((tty), ECHO)
#define L_ECHOE(tty)	_L_FLAG((tty), ECHOE)
#define L_ECHOK(tty)	_L_FLAG((tty), ECHOK)
#define L_ECHONL(tty)	_L_FLAG((tty), ECHONL)
#define L_NOFLSH(tty)	_L_FLAG((tty), NOFLSH)
#define L_TOSTOP(tty)	_L_FLAG((tty), TOSTOP)
#define L_ECHOCTL(tty)	_L_FLAG((tty), ECHOCTL)
#define L_ECHOPRT(tty)	_L_FLAG((tty), ECHOPRT)
#define L_ECHOKE(tty)	_L_FLAG((tty), ECHOKE)
#define L_FLUSHO(tty)	_L_FLAG((tty), FLUSHO)
#define L_PENDIN(tty)	_L_FLAG((tty), PENDIN)
#define L_IEXTEN(tty)	_L_FLAG((tty), IEXTEN)
#define L_EXTPROC(tty)	_L_FLAG((tty), EXTPROC)

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
void stop_tty(struct tty_struct *tty);
void start_tty(struct tty_struct *tty);
void tty_write_message(struct tty_struct *tty, char *msg);
int tty_send_xchar(struct tty_struct *tty, char ch);
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
void tty_termios_encode_baud_rate(struct ktermios *termios, speed_t ibaud,
		speed_t obaud);
void tty_encode_baud_rate(struct tty_struct *tty, speed_t ibaud,
		speed_t obaud);

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
