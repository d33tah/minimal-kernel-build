#ifndef __ASYNC_H__
#define __ASYNC_H__
#include <linux/types.h>
#include <linux/list.h>
#include <linux/numa.h>
#include <linux/device.h>
typedef u64 async_cookie_t;
typedef void (*async_func_t) (void *data, async_cookie_t cookie);
struct async_domain { struct list_head pending; };
/* async_schedule_node_domain, async_dfl_domain removed - never called */
/* async_schedule_domain, async_schedule_dev removed - never called */
/* async_synchronize_full, async_synchronize_cookie_domain removed - runs synchronously */
#endif
