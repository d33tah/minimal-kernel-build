/* Stub async scheduler - runs synchronously */
#include <linux/async.h>
struct async_domain async_dfl_domain = { .pending = LIST_HEAD_INIT(
						 async_dfl_domain.pending),
					 .registered = 0 };
async_cookie_t async_schedule_node_domain(async_func_t func, void *data,
					  int node, struct async_domain *domain)
{
	func(data, 0);
	return 0;
}
