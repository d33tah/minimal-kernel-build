 
/* Minimal stub for dev_printk.h - all device printing disabled */

#ifndef _DEVICE_PRINTK_H_
#define _DEVICE_PRINTK_H_

#include <linux/compiler.h>

#ifndef dev_fmt
#define dev_fmt(fmt) fmt
#endif

struct device;

#define PRINTK_INFO_SUBSYSTEM_LEN	16
#define PRINTK_INFO_DEVICE_LEN		48

struct dev_printk_info {
	char subsystem[PRINTK_INFO_SUBSYSTEM_LEN];
	char device[PRINTK_INFO_DEVICE_LEN];
};

/* All dev_* print macros stubbed to no-ops */
#define dev_printk(level, dev, fmt, ...) do { } while (0)
#define dev_emerg(dev, fmt, ...) do { } while (0)
#define dev_crit(dev, fmt, ...) do { } while (0)
#define dev_alert(dev, fmt, ...) do { } while (0)
#define dev_err(dev, fmt, ...) do { } while (0)
#define dev_warn(dev, fmt, ...) do { } while (0)
#define dev_notice(dev, fmt, ...) do { } while (0)
#define dev_info(dev, fmt, ...) do { } while (0)
#define dev_dbg(dev, fmt, ...) do { } while (0)

#define dev_emerg_once(dev, fmt, ...) do { } while (0)
#define dev_alert_once(dev, fmt, ...) do { } while (0)
#define dev_crit_once(dev, fmt, ...) do { } while (0)
#define dev_err_once(dev, fmt, ...) do { } while (0)
#define dev_warn_once(dev, fmt, ...) do { } while (0)
#define dev_notice_once(dev, fmt, ...) do { } while (0)
#define dev_info_once(dev, fmt, ...) do { } while (0)
#define dev_dbg_once(dev, fmt, ...) do { } while (0)

#define dev_emerg_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_alert_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_crit_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_err_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_warn_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_notice_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_info_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_dbg_ratelimited(dev, fmt, ...) do { } while (0)

#define dev_vdbg(dev, fmt, ...) do { } while (0)
#define dev_WARN(dev, format, arg...) do { } while (0)
#define dev_WARN_ONCE(dev, condition, format, arg...) (0)

#endif  
