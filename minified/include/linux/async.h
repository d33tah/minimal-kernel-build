#ifndef __ASYNC_H__
#define __ASYNC_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/numa.h>
#include <linux/device.h>

typedef u64 async_cookie_t;
typedef void (*async_func_t) (void *data, async_cookie_t cookie);
/* Stub scheduler ignores the domain; kept as an opaque by-pointer tag. */
struct async_domain {
};

#define ASYNC_DOMAIN_EXCLUSIVE(_name) \
	struct async_domain _name = { }

async_cookie_t async_schedule_node_domain(async_func_t func, void *data,
					  int node,
					  struct async_domain *domain);


static inline async_cookie_t
async_schedule_domain(async_func_t func, void *data,
		      struct async_domain *domain)
{
	return async_schedule_node_domain(func, data, NUMA_NO_NODE, domain);
}
/* Removed: current_is_async - never defined/used */
#endif
