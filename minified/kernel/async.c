// SPDX-License-Identifier: GPL-2.0-only
#include <linux/async.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/sched.h>

/* Stubbed async infrastructure - not needed for minimal kernel */

struct async_domain async_dfl_domain = { .pending = LIST_HEAD_INIT(async_dfl_domain.pending), .registered = 0 };

async_cookie_t async_schedule_node_domain(async_func_t func, void *data,
					   int node, struct async_domain *domain)
{
	/* Execute synchronously instead of async */
	func(data, 0);
	return 0;
}

async_cookie_t async_schedule_node(async_func_t func, void *data, int node)
{
	return async_schedule_node_domain(func, data, node, &async_dfl_domain);
}

void async_synchronize_full(void)
{
	/* Nothing to synchronize in stub */
}

void async_synchronize_full_domain(struct async_domain *domain)
{
	/* Nothing to synchronize in stub */
}

void async_synchronize_cookie_domain(async_cookie_t cookie, struct async_domain *domain)
{
	/* Nothing to synchronize in stub */
}

void async_synchronize_cookie(async_cookie_t cookie)
{
	/* Nothing to synchronize in stub */
}
