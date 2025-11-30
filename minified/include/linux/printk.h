 
#ifndef __KERNEL_PRINTK__
#define __KERNEL_PRINTK__

#include <linux/stdarg.h>
#include <linux/init.h>
#include <linux/kern_levels.h>
#include <linux/linkage.h>
#include <linux/ratelimit_types.h>


extern const char linux_banner[];

extern int oops_in_progress;	 

#define PRINTK_MAX_SINGLE_HEADER_LEN 2

static inline int printk_get_level(const char *buffer)
{
	if (buffer[0] == KERN_SOH_ASCII && buffer[1]) {
		switch (buffer[1]) {
		case '0' ... '7':
		case 'c':	 
			return buffer[1];
		}
	}
	return 0;
}

static inline const char *printk_skip_level(const char *buffer)
{
	if (printk_get_level(buffer))
		return buffer + 2;

	return buffer;
}

static inline const char *printk_skip_headers(const char *buffer)
{
	while (printk_get_level(buffer))
		buffer = printk_skip_level(buffer);

	return buffer;
}

#define CONSOLE_EXT_LOG_MAX	8192

 
#define MESSAGE_LOGLEVEL_DEFAULT CONFIG_MESSAGE_LOGLEVEL_DEFAULT

 
#define CONSOLE_LOGLEVEL_SILENT  0  
#define CONSOLE_LOGLEVEL_MIN	 1  
#define CONSOLE_LOGLEVEL_DEBUG	10  
#define CONSOLE_LOGLEVEL_MOTORMOUTH 15	 

 
#define CONSOLE_LOGLEVEL_DEFAULT CONFIG_CONSOLE_LOGLEVEL_DEFAULT
#define CONSOLE_LOGLEVEL_QUIET	 CONFIG_CONSOLE_LOGLEVEL_QUIET

extern int console_printk[];

#define console_loglevel (console_printk[0])
#define default_message_loglevel (console_printk[1])
#define minimum_console_loglevel (console_printk[2])
#define default_console_loglevel (console_printk[3])

extern void console_verbose(void);

struct ctl_table;

extern int suppress_printk;

struct va_format {
	const char *fmt;
	va_list *va;
};

 
#define FW_BUG		"[Firmware Bug]: "
#define FW_WARN		"[Firmware Warn]: "
#define FW_INFO		"[Firmware Info]: "

 
#define HW_ERR		"[Hardware Error]: "

 
#define DEPRECATED	"[Deprecated]: "

 
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
static inline bool printk_timed_ratelimit(unsigned long *caller_jiffies,
					  unsigned int interval_msec)
{
	return false;
}

static inline void wake_up_klogd(void)
{
}

static inline void setup_log_buf(int early)
{
}

static inline void dump_stack_print_info(const char *log_lvl)
{
}

static inline void show_regs_print_info(const char *log_lvl)
{
}

static inline void dump_stack_lvl(const char *log_lvl)
{
}

static inline void dump_stack(void)
{
}


#define __printk_cpu_sync_try_get() true
#define __printk_cpu_sync_wait()
#define __printk_cpu_sync_put()

 
#define printk_cpu_sync_get_irqsave(flags)		\
	for (;;) {					\
		local_irq_save(flags);			\
		if (__printk_cpu_sync_try_get())	\
			break;				\
		local_irq_restore(flags);		\
		__printk_cpu_sync_wait();		\
	}

 
#define printk_cpu_sync_put_irqrestore(flags)	\
	do {					\
		__printk_cpu_sync_put();	\
		local_irq_restore(flags);	\
	} while (0)

extern int kptr_restrict;

 
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

 
#ifdef DEBUG
#define pr_devel(fmt, ...) \
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_devel(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif


 
#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
#include <linux/dynamic_debug.h>

 
#define pr_debug(fmt, ...)			\
	dynamic_pr_debug(fmt, ##__VA_ARGS__)
#elif defined(DEBUG)
#define pr_debug(fmt, ...) \
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

 

#define printk_once(fmt, ...)					\
	no_printk(fmt, ##__VA_ARGS__)
#define printk_deferred_once(fmt, ...)				\
	no_printk(fmt, ##__VA_ARGS__)

#define pr_emerg_once(fmt, ...)					\
	printk_once(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert_once(fmt, ...)					\
	printk_once(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit_once(fmt, ...)					\
	printk_once(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err_once(fmt, ...)					\
	printk_once(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_once(fmt, ...)					\
	printk_once(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice_once(fmt, ...)				\
	printk_once(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_once(fmt, ...)					\
	printk_once(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
 

#if defined(DEBUG)
#define pr_devel_once(fmt, ...)					\
	printk_once(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_devel_once(fmt, ...)					\
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

 
#if defined(DEBUG)
#define pr_debug_once(fmt, ...)					\
	printk_once(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug_once(fmt, ...)					\
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

 
#define printk_ratelimited(fmt, ...)					\
	no_printk(fmt, ##__VA_ARGS__)

#define pr_emerg_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
 

#if defined(DEBUG)
#define pr_devel_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_devel_ratelimited(fmt, ...)					\
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

 
#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
 
#define pr_debug_ratelimited(fmt, ...)					\
do {									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	DEFINE_DYNAMIC_DEBUG_METADATA(descriptor, pr_fmt(fmt));		\
	if (DYNAMIC_DEBUG_BRANCH(descriptor) &&				\
	    __ratelimit(&_rs))						\
		__dynamic_pr_debug(&descriptor, pr_fmt(fmt), ##__VA_ARGS__);	\
} while (0)
#elif defined(DEBUG)
#define pr_debug_ratelimited(fmt, ...)					\
	printk_ratelimited(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug_ratelimited(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

enum {
	DUMP_PREFIX_NONE,
	DUMP_PREFIX_ADDRESS,
	DUMP_PREFIX_OFFSET
};
static inline void print_hex_dump(const char *level, const char *prefix_str,
				  int prefix_type, int rowsize, int groupsize,
				  const void *buf, size_t len, bool ascii)
{
}
static inline void print_hex_dump_bytes(const char *prefix_str, int prefix_type,
					const void *buf, size_t len)
{
}


#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
#define print_hex_dump_debug(prefix_str, prefix_type, rowsize,	\
			     groupsize, buf, len, ascii)	\
	dynamic_hex_dump(prefix_str, prefix_type, rowsize,	\
			 groupsize, buf, len, ascii)
#elif defined(DEBUG)
#define print_hex_dump_debug(prefix_str, prefix_type, rowsize,		\
			     groupsize, buf, len, ascii)		\
	print_hex_dump(KERN_DEBUG, prefix_str, prefix_type, rowsize,	\
		       groupsize, buf, len, ascii)
#else
static inline void print_hex_dump_debug(const char *prefix_str, int prefix_type,
					int rowsize, int groupsize,
					const void *buf, size_t len, bool ascii)
{
}
#endif

 
#define print_hex_dump_bytes(prefix_str, prefix_type, buf, len)	\
	print_hex_dump_debug(prefix_str, prefix_type, 16, 1, buf, len, true)

#endif
