/* Stub async scheduler - runs synchronously */
#include <linux/async.h>


async_cookie_t async_schedule_node_domain(async_func_t func, void *data,
					   int node, struct async_domain *domain)
{

	func(data, 0);
	return 0;
}
