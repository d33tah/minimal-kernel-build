#ifndef __KERNEL_PRINTK__
#define __KERNEL_PRINTK__

#include <linux/init.h>

#define KERN_SOH "\001"
#define KERN_EMERG KERN_SOH "0"
#define KERN_ERR KERN_SOH "3"
#define KERN_WARNING KERN_SOH "4"
#define KERN_INFO KERN_SOH "6"
#define KERN_DEBUG KERN_SOH "7"
#include <linux/ratelimit_types.h>

#define no_printk(fmt, ...)                         \
	({                                          \
		if (0)                              \
			printk(fmt, ##__VA_ARGS__); \
		0;                                  \
	})

static inline __printf(1, 2) __cold void early_printk(const char *s, ...)
{
}

static inline __printf(1, 2) __cold int _printk(const char *s, ...)
{
	return 0;
}
static inline __printf(1, 2) __cold int _printk_deferred(const char *s, ...)
{
	return 0;
}

static inline void dump_stack(void)
{
}

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define __printk_index_emit(...) \
	do {                     \
	} while (0)

#define printk_index_wrap(_p_func, _fmt, ...)          \
	({                                             \
		__printk_index_emit(_fmt, NULL, NULL); \
		_p_func(_fmt, ##__VA_ARGS__);          \
	})

#define printk(fmt, ...) printk_index_wrap(_printk, fmt, ##__VA_ARGS__)
#define printk_deferred(fmt, ...) \
	printk_index_wrap(_printk_deferred, fmt, ##__VA_ARGS__)

#define pr_emerg(fmt, ...) printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...) printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)

#define printk_once(fmt, ...) no_printk(fmt, ##__VA_ARGS__)

#define pr_warn_once(fmt, ...) \
	printk_once(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)

#define printk_ratelimited(fmt, ...) no_printk(fmt, ##__VA_ARGS__)

#define pr_emerg_ratelimited(fmt, ...) \
	printk_ratelimited(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_ratelimited(fmt, ...) \
	printk_ratelimited(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)

#endif
