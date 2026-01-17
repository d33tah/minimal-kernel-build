#ifndef __ASYNC_H__
#define __ASYNC_H__
#include <linux/types.h>
#include <linux/list.h>
#include <linux/numa.h>
#include <linux/device.h>
typedef u64 async_cookie_t;
typedef void (*async_func_t) (void *data, async_cookie_t cookie);
struct async_domain { struct list_head pending; unsigned registered:1; };
async_cookie_t async_schedule_node_domain(async_func_t func, void *data, int node, struct async_domain *domain);
extern struct async_domain async_dfl_domain;
/* async_schedule_domain removed - never called */
static inline async_cookie_t async_schedule_dev(async_func_t func, struct device *dev) { return async_schedule_node_domain(func, dev, dev_to_node(dev), &async_dfl_domain); }
/* async_synchronize_full, async_synchronize_cookie_domain removed - runs synchronously */
#endif
