#ifndef __KERNEL_PRINTK__
#define __KERNEL_PRINTK__

#include <linux/stdarg.h>
#include <linux/init.h>
#include <linux/linkage.h>

#define KERN_SOH	"\001"
#define KERN_SOH_ASCII	'\001'
#define KERN_EMERG	KERN_SOH "0"
#define KERN_ALERT	KERN_SOH "1"
#define KERN_CRIT	KERN_SOH "2"
#define KERN_ERR	KERN_SOH "3"
#define KERN_WARNING	KERN_SOH "4"
#define KERN_NOTICE	KERN_SOH "5"
#define KERN_INFO	KERN_SOH "6"
#define KERN_DEBUG	KERN_SOH "7"
#define KERN_DEFAULT	""
#define KERN_CONT	KERN_SOH "c"
#include <linux/ratelimit_types.h>


extern const char linux_banner[];

extern int oops_in_progress;

#define CONSOLE_EXT_LOG_MAX	8192

#define MESSAGE_LOGLEVEL_DEFAULT CONFIG_MESSAGE_LOGLEVEL_DEFAULT

#define CONSOLE_LOGLEVEL_MIN	 1
#define CONSOLE_LOGLEVEL_MOTORMOUTH 15

#define CONSOLE_LOGLEVEL_DEFAULT CONFIG_CONSOLE_LOGLEVEL_DEFAULT

extern int console_printk[];

#define console_loglevel (console_printk[0])
#define default_message_loglevel (console_printk[1])
#define minimum_console_loglevel (console_printk[2])
#define default_console_loglevel (console_printk[3])

extern void console_verbose(void);

struct ctl_table;

struct va_format {
	const char *fmt;
	va_list *va;
};

#define no_printk(fmt, ...)				\
({							\
	if (0)						\
		printk(fmt, ##__VA_ARGS__);		\
	0;						\
})

static inline __printf(1, 2) __cold
void early_printk(const char *s, ...) { }

struct dev_printk_info;

static inline __printf(1, 0)
int vprintk(const char *s, va_list args)
{
	return 0;
}
static inline __printf(1, 2) __cold
int _printk(const char *s, ...)
{
	return 0;
}
static inline __printf(1, 2) __cold
int _printk_deferred(const char *s, ...)
{
	return 0;
}

static inline bool pr_flush(int timeout_ms, bool reset_on_progress)
{
	return true;
}

static inline int printk_ratelimit(void)
{
	return 0;
}

static inline void wake_up_klogd(void)
{
}

static inline void setup_log_buf(int early)
{
}

static inline void dump_stack(void)
{
}

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

struct module;

#define __printk_index_emit(...) do {} while (0)

#define printk_index_subsys_emit(subsys_fmt_prefix, level, fmt, ...) \
	__printk_index_emit(fmt, level, subsys_fmt_prefix)

#define printk_index_wrap(_p_func, _fmt, ...)				\
	({								\
		__printk_index_emit(_fmt, NULL, NULL);			\
		_p_func(_fmt, ##__VA_ARGS__);				\
	})


#define printk(fmt, ...) printk_index_wrap(_printk, fmt, ##__VA_ARGS__)
#define printk_deferred(fmt, ...)					\
	printk_index_wrap(_printk_deferred, fmt, ##__VA_ARGS__)

#define pr_emerg(fmt, ...) \
	printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert(fmt, ...) \
	printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...) \
	printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) \
	printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...) \
	printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice(fmt, ...) \
	printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)

#define pr_cont(fmt, ...) \
	printk(KERN_CONT fmt, ##__VA_ARGS__)

/* DEBUG and CONFIG_DYNAMIC_DEBUG not defined */
#define pr_devel(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

#define pr_debug(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)


#define printk_once(fmt, ...)					\
	no_printk(fmt, ##__VA_ARGS__)
#define printk_deferred_once(fmt, ...)				\
	no_printk(fmt, ##__VA_ARGS__)

#define pr_err_once(fmt, ...)					\
	printk_once(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_once(fmt, ...)					\
	printk_once(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_once(fmt, ...)					\
	printk_once(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)


#define printk_ratelimited(fmt, ...)					\
	no_printk(fmt, ##__VA_ARGS__)

#define pr_emerg_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)


#endif
