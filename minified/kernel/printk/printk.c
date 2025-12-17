
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/nmi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/memblock.h>
#include <linux/syscalls.h>
#include <linux/ratelimit.h>
#include <linux/kmsg_dump.h>

#include <linux/cpu.h>
#include <linux/rculist.h>
#include <linux/poll.h>
#include <linux/irq_work.h>
#include <linux/ctype.h>
#include <linux/uio.h>
#include <linux/sched/clock.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>

#include <linux/uaccess.h>
#include <asm/sections.h>



/* --- 2025-12-07 23:45 --- Inlined from printk_ringbuffer.h */
struct printk_info {
	u64	seq;
	u64	ts_nsec;
	u16	text_len;
	u8	facility;
	u8	flags:5;
	u8	level:3;
	u32	caller_id;
	struct dev_printk_info	dev_info;
};

struct printk_record {
	struct printk_info	*info;
	char			*text_buf;
	unsigned int		text_buf_size;
};

struct prb_data_blk_lpos {
	unsigned long	begin;
	unsigned long	next;
};

struct prb_desc {
	atomic_long_t			state_var;
	struct prb_data_blk_lpos	text_blk_lpos;
};

struct prb_data_ring {
	unsigned int	size_bits;
	char		*data;
	atomic_long_t	head_lpos;
	atomic_long_t	tail_lpos;
};

struct prb_desc_ring {
	unsigned int		count_bits;
	struct prb_desc		*descs;
	struct printk_info	*infos;
	atomic_long_t		head_id;
	atomic_long_t		tail_id;
	atomic_long_t		last_finalized_id;
};

struct printk_ringbuffer {
	struct prb_desc_ring	desc_ring;
	struct prb_data_ring	text_data_ring;
	atomic_long_t		fail;
};

struct prb_reserved_entry {
	struct printk_ringbuffer	*rb;
	unsigned long			irqflags;
	unsigned long			id;
	unsigned int			text_space;
};

enum desc_state {
	desc_miss	=  -1,
	desc_reserved	= 0x0,
	desc_committed	= 0x1,
	desc_finalized	= 0x2,
	desc_reusable	= 0x3,
};

#define _DATA_SIZE(sz_bits)	(1UL << (sz_bits))
#define _DESCS_COUNT(ct_bits)	(1U << (ct_bits))
#define DESC_SV_BITS		(sizeof(unsigned long) * 8)
#define DESC_FLAGS_SHIFT	(DESC_SV_BITS - 2)
#define DESC_FLAGS_MASK		(3UL << DESC_FLAGS_SHIFT)
#define DESC_STATE(sv)		(3UL & (sv >> DESC_FLAGS_SHIFT))
#define DESC_SV(id, state)	(((unsigned long)state << DESC_FLAGS_SHIFT) | id)
#define DESC_ID_MASK		(~DESC_FLAGS_MASK)
#define DESC_ID(sv)		((sv) & DESC_ID_MASK)
#define FAILED_LPOS		0x1
#define NO_LPOS			0x3

#define FAILED_BLK_LPOS	\
{				\
	.begin	= FAILED_LPOS,	\
	.next	= FAILED_LPOS,	\
}

#define BLK0_LPOS(sz_bits)	(-(_DATA_SIZE(sz_bits)))
#define DESC0_ID(ct_bits)	DESC_ID(-(_DESCS_COUNT(ct_bits) + 1))
#define DESC0_SV(ct_bits)	DESC_SV(DESC0_ID(ct_bits), desc_reusable)

#define _DEFINE_PRINTKRB(name, descbits, avgtextbits, text_buf)			\
static struct prb_desc _##name##_descs[_DESCS_COUNT(descbits)] = {				\
	[_DESCS_COUNT(descbits) - 1] = {							\
		.state_var	= ATOMIC_INIT(DESC0_SV(descbits)),				\
		.text_blk_lpos	= FAILED_BLK_LPOS,						\
	},											\
};												\
static struct printk_info _##name##_infos[_DESCS_COUNT(descbits)] = {				\
	[0] = {											\
		.seq = -(u64)_DESCS_COUNT(descbits),						\
	},											\
	[_DESCS_COUNT(descbits) - 1] = {							\
		.seq = 0,									\
	},											\
};												\
static struct printk_ringbuffer name = {							\
	.desc_ring = {										\
		.count_bits	= descbits,							\
		.descs		= &_##name##_descs[0],						\
		.infos		= &_##name##_infos[0],						\
		.head_id	= ATOMIC_INIT(DESC0_ID(descbits)),				\
		.tail_id	= ATOMIC_INIT(DESC0_ID(descbits)),				\
		.last_finalized_id = ATOMIC_INIT(DESC0_ID(descbits)),				\
	},											\
	.text_data_ring = {									\
		.size_bits	= (avgtextbits) + (descbits),					\
		.data		= text_buf,							\
		.head_lpos	= ATOMIC_LONG_INIT(BLK0_LPOS((avgtextbits) + (descbits))),	\
		.tail_lpos	= ATOMIC_LONG_INIT(BLK0_LPOS((avgtextbits) + (descbits))),	\
	},											\
	.fail			= ATOMIC_LONG_INIT(0),						\
}

#define DEFINE_PRINTKRB(name, descbits, avgtextbits)				\
static char _##name##_text[1U << ((avgtextbits) + (descbits))]			\
			__aligned(__alignof__(unsigned long));			\
_DEFINE_PRINTKRB(name, descbits, avgtextbits, &_##name##_text[0])

/* prb_rec_init_wr removed - never called */

bool prb_reserve(struct prb_reserved_entry *e, struct printk_ringbuffer *rb,
		 struct printk_record *r);
bool prb_reserve_in_last(struct prb_reserved_entry *e, struct printk_ringbuffer *rb,
			 struct printk_record *r, u32 caller_id, unsigned int max_size);
void prb_commit(struct prb_reserved_entry *e);
void prb_final_commit(struct prb_reserved_entry *e);

void prb_init(struct printk_ringbuffer *rb,
	      char *text_buf, unsigned int text_buf_size,
	      struct prb_desc *descs, unsigned int descs_count_bits,
	      struct printk_info *infos);
unsigned int prb_record_text_space(struct prb_reserved_entry *e);

static inline void prb_rec_init_rd(struct printk_record *r,
				   struct printk_info *info,
				   char *text_buf, unsigned int text_buf_size)
{
	r->info = info;
	r->text_buf = text_buf;
	r->text_buf_size = text_buf_size;
}

#define prb_for_each_record(from, rb, s, r) \
for ((s) = from; prb_read_valid(rb, s, r); (s) = (r)->info->seq + 1)

#define prb_for_each_info(from, rb, s, i, lc) \
for ((s) = from; prb_read_valid_info(rb, s, i, lc); (s) = (i)->seq + 1)

bool prb_read_valid(struct printk_ringbuffer *rb, u64 seq,
		    struct printk_record *r);
bool prb_read_valid_info(struct printk_ringbuffer *rb, u64 seq,
			 struct printk_info *info, unsigned int *line_count);

u64 prb_first_valid_seq(struct printk_ringbuffer *rb);
u64 prb_next_seq(struct printk_ringbuffer *rb);
/* end printk_ringbuffer.h */
/* console_cmdline.h inlined */
struct console_cmdline { char name[16]; int index; bool user_specified; char *options; };
/* end console_cmdline.h */
/* braille.h inlined - stub for _braille_register_console only (others unused) */
static inline int _braille_register_console(struct console *console, struct console_cmdline *c) { return 0; }
/* end braille.h */
#include "internal.h"

int console_printk[4] = {
	CONSOLE_LOGLEVEL_DEFAULT,	 
	MESSAGE_LOGLEVEL_DEFAULT,	 
	CONSOLE_LOGLEVEL_MIN,		 
	CONSOLE_LOGLEVEL_DEFAULT,	 
};

atomic_t ignore_console_lock_warning __read_mostly = ATOMIC_INIT(0);

int oops_in_progress;

static DEFINE_SEMAPHORE(console_sem);
struct console *console_drivers;

static int __read_mostly suppress_panic_printk;

/* control_devkmsg and __setup removed (~2 LOC) */

static int nr_ext_console_drivers;

#define down_console_sem() do { \
	down(&console_sem);\
	mutex_acquire(&console_lock_dep_map, 0, 0, _RET_IP_);\
} while (0)

static int __down_trylock_console_sem(unsigned long ip)
{
	int lock_failed;
	unsigned long flags;

	 
	printk_safe_enter_irqsave(flags);
	lock_failed = down_trylock(&console_sem);
	printk_safe_exit_irqrestore(flags);

	if (lock_failed)
		return 1;
	mutex_acquire(&console_lock_dep_map, 0, 1, ip);
	return 0;
}
#define down_trylock_console_sem() __down_trylock_console_sem(_RET_IP_)

static void __up_console_sem(unsigned long ip)
{
	unsigned long flags;

	mutex_release(&console_lock_dep_map, ip);

	printk_safe_enter_irqsave(flags);
	up(&console_sem);
	printk_safe_exit_irqrestore(flags);
}
#define up_console_sem() __up_console_sem(_RET_IP_)

static bool panic_in_progress(void)
{
	return unlikely(atomic_read(&panic_cpu) != PANIC_CPU_INVALID);
}

static int console_locked, console_suspended;


#define MAX_CMDLINECONSOLES 8

static struct console_cmdline console_cmdline[MAX_CMDLINECONSOLES];

static int preferred_console = -1;
/* console_set_on_cmdline removed - never used */

static int console_may_schedule;

enum con_msg_format_flags {
	MSG_FORMAT_DEFAULT	= 0,
	MSG_FORMAT_SYSLOG	= (1 << 0),
};

static int console_msg_format = MSG_FORMAT_DEFAULT;


static DEFINE_MUTEX(syslog_lock);


#define CONSOLE_LOG_MAX		0
#define DROPPED_TEXT_MAX	0
#define printk_time		false

#define prb_read_valid(rb, seq, r)	false
#define prb_first_valid_seq(rb)		0
#define prb_next_seq(rb)		0

static u64 syslog_seq;

static size_t record_print_text(const struct printk_record *r,
				bool syslog, bool time)
{
	return 0;
}
static ssize_t info_print_ext_header(char *buf, size_t size,
				     struct printk_info *info)
{
	return 0;
}
static ssize_t msg_print_ext_body(char *buf, size_t size,
				  char *text, size_t text_len,
				  struct dev_printk_info *dev_info) { return 0; }
static void console_lock_spinning_enable(void) { }
static int console_lock_spinning_disable_and_check(void) { return 0; }
static void call_console_driver(struct console *con, const char *text, size_t len,
				char *dropped_text)
{
}
static bool suppress_message_printing(int level) { return false; }




/* console_msg_format_setup, console_setup and __setup handlers removed (~6 LOC) */

/* add_preferred_console removed - no callers */

/* console_suspend_enabled removed - unused */
static bool printk_console_no_auto_verbose;

void console_verbose(void)
{
	if (console_loglevel && !printk_console_no_auto_verbose)
		console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
}

void console_lock(void)
{
	might_sleep();

	down_console_sem();
	if (console_suspended)
		return;
	console_locked = 1;
	console_may_schedule = 1;
}

int console_trylock(void)
{
	if (down_trylock_console_sem())
		return 0;
	if (console_suspended) {
		up_console_sem();
		return 0;
	}
	console_locked = 1;
	console_may_schedule = 0;
	return 1;
}

int is_console_locked(void)
{
	return console_locked;
}

static bool abandon_console_lock_in_panic(void)
{
	if (!panic_in_progress())
		return false;

	 
	return atomic_read(&panic_cpu) != raw_smp_processor_id();
}

static inline bool console_is_usable(struct console *con)
{
	if (!(con->flags & CON_ENABLED))
		return false;

	if (!con->write)
		return false;

	 
	if (!cpu_online(raw_smp_processor_id()) &&
	    !(con->flags & CON_ANYTIME))
		return false;

	return true;
}

static void __console_unlock(void)
{
	console_locked = 0;
	up_console_sem();
}

static bool console_emit_next_record(struct console *con, char *text, char *ext_text,
				     char *dropped_text, bool *handover)
{
	static int panic_console_dropped;
	struct printk_info info;
	struct printk_record r;
	unsigned long flags;
	char *write_text;
	size_t len;

	prb_rec_init_rd(&r, &info, text, CONSOLE_LOG_MAX);

	*handover = false;

	if (!prb_read_valid(prb, con->seq, &r))
		return false;

	if (con->seq != r.info->seq) {
		con->dropped += r.info->seq - con->seq;
		con->seq = r.info->seq;
		if (panic_in_progress() && panic_console_dropped++ > 10) {
			suppress_panic_printk = 1;
		}
	}

	 
	if (suppress_message_printing(r.info->level)) {
		con->seq++;
		goto skip;
	}

	if (ext_text) {
		write_text = ext_text;
		len = info_print_ext_header(ext_text, CONSOLE_EXT_LOG_MAX, r.info);
		len += msg_print_ext_body(ext_text + len, CONSOLE_EXT_LOG_MAX - len,
					  &r.text_buf[0], r.info->text_len, &r.info->dev_info);
	} else {
		write_text = text;
		len = record_print_text(&r, console_msg_format & MSG_FORMAT_SYSLOG, printk_time);
	}

	 
	printk_safe_enter_irqsave(flags);
	console_lock_spinning_enable();

	stop_critical_timings();	 
	call_console_driver(con, write_text, len, dropped_text);
	start_critical_timings();

	con->seq++;

	*handover = console_lock_spinning_disable_and_check();
	printk_safe_exit_irqrestore(flags);
skip:
	return true;
}

static bool console_flush_all(bool do_cond_resched, u64 *next_seq, bool *handover)
{
	static char dropped_text[DROPPED_TEXT_MAX];
	static char ext_text[CONSOLE_EXT_LOG_MAX];
	static char text[CONSOLE_LOG_MAX];
	bool any_usable = false;
	struct console *con;
	bool any_progress;

	*next_seq = 0;
	*handover = false;

	do {
		any_progress = false;

		for_each_console(con) {
			bool progress;

			if (!console_is_usable(con))
				continue;
			any_usable = true;

			if (con->flags & CON_EXTENDED) {
				 
				progress = console_emit_next_record(con, &text[0],
								    &ext_text[0], NULL,
								    handover);
			} else {
				progress = console_emit_next_record(con, &text[0],
								    NULL, &dropped_text[0],
								    handover);
			}
			if (*handover)
				return false;

			 
			if (con->seq > *next_seq)
				*next_seq = con->seq;

			if (!progress)
				continue;
			any_progress = true;

			 
			if (abandon_console_lock_in_panic())
				return false;

			if (do_cond_resched)
				cond_resched();
		}
	} while (any_progress);

	return any_usable;
}

void console_unlock(void)
{
	bool do_cond_resched;
	bool handover;
	bool flushed;
	u64 next_seq;

	if (console_suspended) {
		up_console_sem();
		return;
	}

	 
	do_cond_resched = console_may_schedule;

	do {
		console_may_schedule = 0;

		flushed = console_flush_all(do_cond_resched, &next_seq, &handover);
		if (!handover)
			__console_unlock();

		 
		if (!flushed)
			break;

		 
	} while (prb_read_valid(prb, next_seq, NULL) && console_trylock());
}

/* console_conditional_schedule removed - no callers */

void console_unblank(void)
{
	struct console *c;

	 
	if (oops_in_progress) {
		if (down_trylock_console_sem() != 0)
			return;
	} else
		console_lock();

	console_locked = 1;
	console_may_schedule = 0;
	for_each_console(c)
		if ((c->flags & CON_ENABLED) && c->unblank)
			c->unblank();
	console_unlock();

	if (!oops_in_progress)
		pr_flush(1000, true);
}

void console_flush_on_panic(enum con_flush_mode mode)
{
	 
	console_trylock();
	console_may_schedule = 0;

	if (mode == CONSOLE_REPLAY_ALL) {
		struct console *c;
		u64 seq;

		seq = prb_first_valid_seq(prb);
		for_each_console(c)
			c->seq = seq;
	}
	console_unlock();
}

struct tty_driver *console_device(int *index)
{
	struct console *c;
	struct tty_driver *driver = NULL;

	console_lock();
	for_each_console(c) {
		if (!c->device)
			continue;
		driver = c->device(c, index);
		if (driver)
			break;
	}
	console_unlock();
	return driver;
}

/* console_stop, console_start removed - unused */

static int __read_mostly keep_bootcon;
/* keep_bootcon_setup and early_param removed (~2 LOC) */

static int try_enable_preferred_console(struct console *newcon,
					bool user_specified)
{
	struct console_cmdline *c;
	int i, err;

	for (i = 0, c = console_cmdline;
	     i < MAX_CMDLINECONSOLES && c->name[0];
	     i++, c++) {
		if (c->user_specified != user_specified)
			continue;
		if (!newcon->match ||
		    newcon->match(newcon, c->name, c->index, c->options) != 0) {
			 
			BUILD_BUG_ON(sizeof(c->name) != sizeof(newcon->name));
			if (strcmp(c->name, newcon->name) != 0)
				continue;
			if (newcon->index >= 0 &&
			    newcon->index != c->index)
				continue;
			if (newcon->index < 0)
				newcon->index = c->index;

			if (_braille_register_console(newcon, c))
				return 0;

			if (newcon->setup &&
			    (err = newcon->setup(newcon, c->options)) != 0)
				return err;
		}
		newcon->flags |= CON_ENABLED;
		if (i == preferred_console)
			newcon->flags |= CON_CONSDEV;
		return 0;
	}

	 
	if (newcon->flags & CON_ENABLED && c->user_specified ==	user_specified)
		return 0;

	return -ENOENT;
}

static void try_enable_default_console(struct console *newcon)
{
	if (newcon->index < 0)
		newcon->index = 0;

	if (newcon->setup && newcon->setup(newcon, NULL) != 0)
		return;

	newcon->flags |= CON_ENABLED;

	if (newcon->device)
		newcon->flags |= CON_CONSDEV;
}

#define con_printk(lvl, con, fmt, ...)			\
	printk(lvl pr_fmt("%sconsole [%s%d] " fmt),	\
	       (con->flags & CON_BOOT) ? "boot" : "",	\
	       con->name, con->index, ##__VA_ARGS__)

void register_console(struct console *newcon)
{
	struct console *con;
	bool bootcon_enabled = false;
	bool realcon_enabled = false;
	int err;

	for_each_console(con) {
		if (WARN(con == newcon, "console '%s%d' already registered\n",
					 con->name, con->index))
			return;
	}

	for_each_console(con) {
		if (con->flags & CON_BOOT)
			bootcon_enabled = true;
		else
			realcon_enabled = true;
	}

	 
	if (newcon->flags & CON_BOOT && realcon_enabled) {
		return;
	}

	 
	if (preferred_console < 0) {
		if (!console_drivers || !console_drivers->device ||
		    console_drivers->flags & CON_BOOT) {
			try_enable_default_console(newcon);
		}
	}

	 
	err = try_enable_preferred_console(newcon, true);

	 
	if (err == -ENOENT)
		err = try_enable_preferred_console(newcon, false);

	 
	if (err || newcon->flags & CON_BRL)
		return;

	 
	if (bootcon_enabled &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV)) {
		newcon->flags &= ~CON_PRINTBUFFER;
	}

	 
	console_lock();
	if ((newcon->flags & CON_CONSDEV) || console_drivers == NULL) {
		newcon->next = console_drivers;
		console_drivers = newcon;
		if (newcon->next)
			newcon->next->flags &= ~CON_CONSDEV;
		 
		newcon->flags |= CON_CONSDEV;
	} else {
		newcon->next = console_drivers->next;
		console_drivers->next = newcon;
	}

	if (newcon->flags & CON_EXTENDED)
		nr_ext_console_drivers++;

	newcon->dropped = 0;
	if (newcon->flags & CON_PRINTBUFFER) {
		 
		mutex_lock(&syslog_lock);
		newcon->seq = syslog_seq;
		mutex_unlock(&syslog_lock);
	} else {
		 
		newcon->seq = prb_next_seq(prb);
	}
	console_unlock();
	console_sysfs_notify();

	 
	con_printk(KERN_INFO, newcon, "enabled\n");
	if (bootcon_enabled &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV) &&
	    !keep_bootcon) {
		 
		for_each_console(con)
			if (con->flags & CON_BOOT)
				unregister_console(con);
	}
}

int unregister_console(struct console *console)
{
	/* Stub: console unregistration not needed for minimal kernel */
	return 0;
}

void __init console_init(void)
{
	int ret;
	initcall_t call;
	initcall_entry_t *ce;

	 
	n_tty_init();

	 
	ce = __con_initcall_start;
	 
	while (ce < __con_initcall_end) {
		call = initcall_from_entry(ce);
		 
		ret = call();
		 
		ce++;
	}
}

/* printk_late_init removed - not needed for minimal kernel */


