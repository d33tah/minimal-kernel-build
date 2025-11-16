 
 

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
#include <linux/crash_core.h>
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



#include "printk_ringbuffer.h"
#include "console_cmdline.h"
#include "braille.h"
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

 
int __read_mostly suppress_printk;

 
static int __read_mostly suppress_panic_printk;


enum devkmsg_log_bits {
	__DEVKMSG_LOG_BIT_ON = 0,
	__DEVKMSG_LOG_BIT_OFF,
	__DEVKMSG_LOG_BIT_LOCK,
};

enum devkmsg_log_masks {
	DEVKMSG_LOG_MASK_ON             = BIT(__DEVKMSG_LOG_BIT_ON),
	DEVKMSG_LOG_MASK_OFF            = BIT(__DEVKMSG_LOG_BIT_OFF),
	DEVKMSG_LOG_MASK_LOCK           = BIT(__DEVKMSG_LOG_BIT_LOCK),
};

 
#define DEVKMSG_LOG_MASK_DEFAULT	0

static unsigned int __read_mostly devkmsg_log = DEVKMSG_LOG_MASK_DEFAULT;

static int __control_devkmsg(char *str)
{
	size_t len;

	if (!str)
		return -EINVAL;

	len = str_has_prefix(str, "on");
	if (len) {
		devkmsg_log = DEVKMSG_LOG_MASK_ON;
		return len;
	}

	len = str_has_prefix(str, "off");
	if (len) {
		devkmsg_log = DEVKMSG_LOG_MASK_OFF;
		return len;
	}

	len = str_has_prefix(str, "ratelimit");
	if (len) {
		devkmsg_log = DEVKMSG_LOG_MASK_DEFAULT;
		return len;
	}

	return -EINVAL;
}

static int __init control_devkmsg(char *str)
{
	if (__control_devkmsg(str) < 0) {
		return 1;
	}

	 
	if (devkmsg_log == DEVKMSG_LOG_MASK_ON)
		strcpy(devkmsg_log_str, "on");
	else if (devkmsg_log == DEVKMSG_LOG_MASK_OFF)
		strcpy(devkmsg_log_str, "off");
	 

	 
	devkmsg_log |= DEVKMSG_LOG_MASK_LOCK;

	return 1;
}
__setup("printk.devkmsg=", control_devkmsg);

char devkmsg_log_str[DEVKMSG_STR_MAX_SIZE] = "ratelimit";

 
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
int console_set_on_cmdline;

 
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
static bool __pr_flush(struct console *con, int timeout_ms, bool reset_on_progress) { return true; }



static void set_user_specified(struct console_cmdline *c, bool user_specified)
{
	if (!user_specified)
		return;

	 
	c->user_specified = true;
	 
	console_set_on_cmdline = 1;
}

static int __add_preferred_console(char *name, int idx, char *options,
				   char *brl_options, bool user_specified)
{
	struct console_cmdline *c;
	int i;

	 
	for (i = 0, c = console_cmdline;
	     i < MAX_CMDLINECONSOLES && c->name[0];
	     i++, c++) {
		if (strcmp(c->name, name) == 0 && c->index == idx) {
			if (!brl_options)
				preferred_console = i;
			set_user_specified(c, user_specified);
			return 0;
		}
	}
	if (i == MAX_CMDLINECONSOLES)
		return -E2BIG;
	if (!brl_options)
		preferred_console = i;
	strlcpy(c->name, name, sizeof(c->name));
	c->options = options;
	set_user_specified(c, user_specified);
	braille_set_options(c, brl_options);

	c->index = idx;
	return 0;
}

static int __init console_msg_format_setup(char *str)
{
	if (!strcmp(str, "syslog"))
		console_msg_format = MSG_FORMAT_SYSLOG;
	if (!strcmp(str, "default"))
		console_msg_format = MSG_FORMAT_DEFAULT;
	return 1;
}
__setup("console_msg_format=", console_msg_format_setup);

 
static int __init console_setup(char *str)
{
	char buf[sizeof(console_cmdline[0].name) + 4];  
	char *s, *options, *brl_options = NULL;
	int idx;

	 
	if (str[0] == 0 || strcmp(str, "null") == 0) {
		__add_preferred_console("ttynull", 0, NULL, NULL, true);
		return 1;
	}

	if (_braille_console_setup(&str, &brl_options))
		return 1;

	 
	if (str[0] >= '0' && str[0] <= '9') {
		strcpy(buf, "ttyS");
		strncpy(buf + 4, str, sizeof(buf) - 5);
	} else {
		strncpy(buf, str, sizeof(buf) - 1);
	}
	buf[sizeof(buf) - 1] = 0;
	options = strchr(str, ',');
	if (options)
		*(options++) = 0;
#ifdef __sparc__
	if (!strcmp(str, "ttya"))
		strcpy(buf, "ttyS0");
	if (!strcmp(str, "ttyb"))
		strcpy(buf, "ttyS1");
#endif
	for (s = buf; *s; s++)
		if (isdigit(*s) || *s == ',')
			break;
	idx = simple_strtoul(s, NULL, 10);
	*s = 0;

	__add_preferred_console(buf, idx, options, brl_options, true);
	return 1;
}
__setup("console=", console_setup);

 
int add_preferred_console(char *name, int idx, char *options)
{
	return __add_preferred_console(name, idx, options, NULL, false);
}

bool console_suspend_enabled = true;

static int __init console_suspend_disable(char *str)
{
	console_suspend_enabled = false;
	return 1;
}
__setup("no_console_suspend", console_suspend_disable);
module_param_named(console_suspend, console_suspend_enabled,
		bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(console_suspend, "suspend console during suspend"
	" and hibernate operations");

static bool printk_console_no_auto_verbose;

void console_verbose(void)
{
	if (console_loglevel && !printk_console_no_auto_verbose)
		console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
}

module_param_named(console_no_auto_verbose, printk_console_no_auto_verbose, bool, 0644);
MODULE_PARM_DESC(console_no_auto_verbose, "Disable console loglevel raise to highest on oops/panic/etc");

 
void suspend_console(void)
{
	if (!console_suspend_enabled)
		return;
	pr_flush(1000, true);
	console_lock();
	console_suspended = 1;
	up_console_sem();
}

void resume_console(void)
{
	if (!console_suspend_enabled)
		return;
	down_console_sem();
	console_suspended = 0;
	console_unlock();
	pr_flush(1000, true);
}

 
static int console_cpu_notify(unsigned int cpu)
{
	if (!cpuhp_tasks_frozen) {
		 
		if (console_trylock())
			console_unlock();
	}
	return 0;
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

 
void __sched console_conditional_schedule(void)
{
	if (console_may_schedule)
		cond_resched();
}

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

 
void console_stop(struct console *console)
{
	__pr_flush(console, 1000, true);
	console_lock();
	console->flags &= ~CON_ENABLED;
	console_unlock();
}

void console_start(struct console *console)
{
	console_lock();
	console->flags |= CON_ENABLED;
	console_unlock();
	__pr_flush(console, 1000, true);
}

static int __read_mostly keep_bootcon;

static int __init keep_bootcon_setup(char *str)
{
	keep_bootcon = 1;
	return 0;
}

early_param("keep_bootcon", keep_bootcon_setup);

 
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
	struct console *con;
	int res;

	con_printk(KERN_INFO, console, "disabled\n");

	res = _braille_unregister_console(console);
	if (res < 0)
		return res;
	if (res > 0)
		return 0;

	res = -ENODEV;
	console_lock();
	if (console_drivers == console) {
		console_drivers=console->next;
		res = 0;
	} else {
		for_each_console(con) {
			if (con->next == console) {
				con->next = console->next;
				res = 0;
				break;
			}
		}
	}

	if (res)
		goto out_disable_unlock;

	if (console->flags & CON_EXTENDED)
		nr_ext_console_drivers--;

	 
	if (console_drivers != NULL && console->flags & CON_CONSDEV)
		console_drivers->flags |= CON_CONSDEV;

	console->flags &= ~CON_ENABLED;
	console_unlock();
	console_sysfs_notify();

	if (console->exit)
		res = console->exit(console);

	return res;

out_disable_unlock:
	console->flags &= ~CON_ENABLED;
	console_unlock();

	return res;
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

 
static int __init printk_late_init(void)
{
	struct console *con;
	int ret;

	for_each_console(con) {
		if (!(con->flags & CON_BOOT))
			continue;

		 
		if (init_section_intersects(con, sizeof(*con)) ||
		    init_section_contains(con->write, 0) ||
		    init_section_contains(con->read, 0) ||
		    init_section_contains(con->device, 0) ||
		    init_section_contains(con->unblank, 0) ||
		    init_section_contains(con->data, 0)) {
			unregister_console(con);
		}
	}
	ret = cpuhp_setup_state_nocalls(CPUHP_PRINTK_DEAD, "printk:dead", NULL,
					console_cpu_notify);
	WARN_ON(ret < 0);
	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "printk:online",
					console_cpu_notify, NULL);
	WARN_ON(ret < 0);
	printk_sysctl_init();
	return 0;
}
late_initcall(printk_late_init);


