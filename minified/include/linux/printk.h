#ifndef __KERNEL_PRINTK__
#define __KERNEL_PRINTK__

#include <linux/stdarg.h>
#include <linux/init.h>
#include <linux/linkage.h>

#define KERN_SOH	"\001"
#define KERN_EMERG	KERN_SOH "0"
#define KERN_ALERT	KERN_SOH "1"
#define KERN_CRIT	KERN_SOH "2"
#define KERN_ERR	KERN_SOH "3"
#define KERN_WARNING	KERN_SOH "4"
#define KERN_NOTICE	KERN_SOH "5"
#define KERN_INFO	KERN_SOH "6"
#define KERN_DEBUG	KERN_SOH "7"
#define KERN_DEFAULT	""
#include <linux/bits.h>
#include <asm/param.h>
#include <linux/spinlock_types.h>
struct ratelimit_state { raw_spinlock_t lock; int interval; int burst; int printed; int missed; unsigned long begin; unsigned long flags; };
#define RATELIMIT_STATE_INIT_FLAGS(name, interval_init, burst_init, flags_init) { .lock = __RAW_SPIN_LOCK_UNLOCKED(name.lock), .interval = interval_init, .burst = burst_init, .flags = flags_init, }
#define RATELIMIT_STATE_INIT(name, interval_init, burst_init) RATELIMIT_STATE_INIT_FLAGS(name, interval_init, burst_init, 0)

extern const char linux_banner[];

extern int oops_in_progress;

#define MESSAGE_LOGLEVEL_DEFAULT CONFIG_MESSAGE_LOGLEVEL_DEFAULT

#define CONSOLE_LOGLEVEL_MIN	 1
#define CONSOLE_LOGLEVEL_MOTORMOUTH 15

#define CONSOLE_LOGLEVEL_DEFAULT CONFIG_CONSOLE_LOGLEVEL_DEFAULT

extern int console_printk[];

#define console_loglevel (console_printk[0])

extern void console_verbose(void);

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
int _printk(const char *s, ...)
{
	return 0;
}
static inline __printf(1, 2) __cold
int _printk_deferred(const char *s, ...)
{
	return 0;
}

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define printk(fmt, ...) _printk(fmt, ##__VA_ARGS__)
#define printk_deferred(fmt, ...) _printk_deferred(fmt, ##__VA_ARGS__)

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

#define printk_once(fmt, ...)					\
	no_printk(fmt, ##__VA_ARGS__)

#define pr_err_once(fmt, ...)					\
	printk_once(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_once(fmt, ...)					\
	printk_once(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)

#define printk_ratelimited(fmt, ...)					\
	no_printk(fmt, ##__VA_ARGS__)

#define pr_emerg_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)

#endif
